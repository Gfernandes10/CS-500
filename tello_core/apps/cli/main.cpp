#include "tello/tello_client.hpp"
#include "tello/metrics.hpp"
#include "tello/state_receiver.hpp"
#include "tello/video_stream_reader_ffmpeg.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {

std::atomic<bool> g_keep_running{true};
volatile std::sig_atomic_t g_stop_requested = 0;

enum class RunMode {
    WATCH = 0,
    ONCE = 1,
    STATE_WATCH = 2,
    VIDEO_WATCH = 3,
};

struct CliOptions {
    RunMode mode = RunMode::WATCH;
    int32_t interval_ms = 1000;
    int32_t duration_s = 0;
    std::string metrics_csv_path;
    std::string test_id;
    std::string scenario;
    std::string notes;
};

void handleSignal(int signal) {
    (void)signal;
    // First Ctrl+C asks for graceful stop. Second Ctrl+C exits immediately.
    if (g_stop_requested != 0) {
        std::_Exit(130);
    }
    g_stop_requested = 1;
    g_keep_running = false;
}

std::string responseCodeToString(tello::ResponseCode rc) {
    switch (rc) {
        case tello::ResponseCode::OK:
            return "OK";
        case tello::ResponseCode::ERROR:
            return "ERROR";
        case tello::ResponseCode::TIMEOUT:
            return "TIMEOUT";
        case tello::ResponseCode::PARSE_ERROR:
            return "PARSE_ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string connectionStateToString(tello::TelloClient::ConnectionState state) {
    switch (state) {
        case tello::TelloClient::ConnectionState::CONNECTED:
            return "CONNECTED";
        case tello::TelloClient::ConnectionState::RECOVERING:
            return "RECOVERING";
        case tello::TelloClient::ConnectionState::DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

std::string connectionEventToString(tello::TelloClient::ConnectionEvent event) {
    switch (event) {
        case tello::TelloClient::ConnectionEvent::LOST:
            return "LOST";
        case tello::TelloClient::ConnectionEvent::RESTORED:
            return "RESTORED";
        case tello::TelloClient::ConnectionEvent::TRANSIENT_LOSS_RECOVERED:
            return "TRANSIENT_LOSS_RECOVERED";
        case tello::TelloClient::ConnectionEvent::NONE:
            return "NONE";
        default:
            return "UNKNOWN";
    }
}

void printConnectionEvent(const tello::TelloClient& client, tello::TelloClient::ConnectionEvent event) {
    if (event == tello::TelloClient::ConnectionEvent::LOST) {
        std::cout << "[tello_cli] event: connection lost" << std::endl;
    } else if (event == tello::TelloClient::ConnectionEvent::RESTORED) {
        std::cout << "[tello_cli] event: connection restored"
                  << " (after " << client.getLastOutageFailures() << " failed attempts)"
                  << std::endl;
    } else if (event == tello::TelloClient::ConnectionEvent::TRANSIENT_LOSS_RECOVERED) {
        std::cout << "[tello_cli] event: transient command loss recovered within command"
                  << std::endl;
    }
}

std::string runModeToString(RunMode mode) {
    switch (mode) {
        case RunMode::WATCH:
            return "WATCH";
        case RunMode::ONCE:
            return "ONCE";
        case RunMode::STATE_WATCH:
            return "STATE_WATCH";
        case RunMode::VIDEO_WATCH:
            return "VIDEO_WATCH";
        default:
            return "UNKNOWN";
    }
}

bool shouldRestartLocalVideoPipeline(const tello::TelloClient::VideoRecoveryStatus& status) {
    if (!status.attempted || status.result != tello::ResponseCode::OK) {
        return false;
    }
    return status.stage == "power_streamon"
        || status.stage == "streamon_after_reinit"
        || status.stage == "streamon_after_command";
}

void printUsage() {
    std::cout << "Usage: tello_cli [--once | --watch | --state-watch | --video-watch] [--interval-ms N] [--duration-s N] [--metrics-csv PATH] [--test-id ID] [--scenario NAME] [--notes TEXT] [--help]" << std::endl;
    std::cout << "  --once         Run a single command/query cycle and exit." << std::endl;
    std::cout << "  --watch        Run continuous reconnect test loop (default)." << std::endl;
    std::cout << "  --state-watch  Run continuous telemetry state receiver loop." << std::endl;
    std::cout << "  --video-watch  Enter SDK mode, send streamon, and monitor video packets." << std::endl;
    std::cout << "  --interval-ms  Loop interval in milliseconds for watch modes (default: 1000)." << std::endl;
    std::cout << "  --duration-s   Auto-stop watch modes after N seconds (default: 0 = run until Ctrl+C)." << std::endl;
    std::cout << "  --metrics-csv  Export periodic metrics rows to CSV file." << std::endl;
    std::cout << "  --test-id      Experiment/test identifier written to CSV rows." << std::endl;
    std::cout << "  --scenario     Scenario label written to CSV rows." << std::endl;
    std::cout << "  --notes        Free-text notes written to CSV rows." << std::endl;
}

bool parseOptions(int argc, char** argv, CliOptions& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return false;
        }
        if (arg == "--once") {
            options.mode = RunMode::ONCE;
            continue;
        }
        if (arg == "--watch") {
            options.mode = RunMode::WATCH;
            continue;
        }
        if (arg == "--state-watch") {
            options.mode = RunMode::STATE_WATCH;
            continue;
        }
        if (arg == "--video-watch") {
            options.mode = RunMode::VIDEO_WATCH;
            continue;
        }
        if (arg == "--interval-ms") {
            if (i + 1 >= argc) {
                std::cerr << "[tello_cli] Missing value for --interval-ms" << std::endl;
                return false;
            }
            const std::string value = argv[++i];
            try {
                options.interval_ms = std::stoi(value);
            } catch (...) {
                std::cerr << "[tello_cli] Invalid integer for --interval-ms: " << value << std::endl;
                return false;
            }
            if (options.interval_ms <= 0) {
                std::cerr << "[tello_cli] --interval-ms must be > 0" << std::endl;
                return false;
            }
            continue;
        }
        if (arg == "--duration-s") {
            if (i + 1 >= argc) {
                std::cerr << "[tello_cli] Missing value for --duration-s" << std::endl;
                return false;
            }
            const std::string value = argv[++i];
            try {
                options.duration_s = std::stoi(value);
            } catch (...) {
                std::cerr << "[tello_cli] Invalid integer for --duration-s: " << value << std::endl;
                return false;
            }
            if (options.duration_s < 0) {
                std::cerr << "[tello_cli] --duration-s must be >= 0" << std::endl;
                return false;
            }
            continue;
        }
        if (arg == "--metrics-csv") {
            if (i + 1 >= argc) {
                std::cerr << "[tello_cli] Missing value for --metrics-csv" << std::endl;
                return false;
            }
            options.metrics_csv_path = argv[++i];
            continue;
        }
        if (arg == "--test-id") {
            if (i + 1 >= argc) {
                std::cerr << "[tello_cli] Missing value for --test-id" << std::endl;
                return false;
            }
            options.test_id = argv[++i];
            continue;
        }
        if (arg == "--scenario") {
            if (i + 1 >= argc) {
                std::cerr << "[tello_cli] Missing value for --scenario" << std::endl;
                return false;
            }
            options.scenario = argv[++i];
            continue;
        }
        if (arg == "--notes") {
            if (i + 1 >= argc) {
                std::cerr << "[tello_cli] Missing value for --notes" << std::endl;
                return false;
            }
            options.notes = argv[++i];
            continue;
        }

        std::cerr << "[tello_cli] Unknown argument: " << arg << std::endl;
        printUsage();
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char** argv) {
    CliOptions options;
    if (!parseOptions(argc, argv, options)) {
        return 2;
    }

    const auto run_started_at = std::chrono::steady_clock::now();
    const bool has_duration_limit = options.duration_s > 0;
    auto shouldStopByDuration = [&]() -> bool {
        if (!has_duration_limit) {
            return false;
        }
        const auto elapsed = std::chrono::steady_clock::now() - run_started_at;
        const auto elapsed_s = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        return elapsed_s >= options.duration_s;
    };

    std::signal(SIGINT, handleSignal);

    if (options.mode == RunMode::WATCH) {
        std::cout << "[tello_cli] Reconnect test mode starting..." << std::endl;
        std::cout << "[tello_cli] Press Ctrl+C to stop." << std::endl;
    } else if (options.mode == RunMode::STATE_WATCH) {
        std::cout << "[tello_cli] Telemetry state-watch mode starting..." << std::endl;
        std::cout << "[tello_cli] Press Ctrl+C to stop." << std::endl;
    } else if (options.mode == RunMode::VIDEO_WATCH) {
        std::cout << "[tello_cli] Video watch mode starting..." << std::endl;
        std::cout << "[tello_cli] Press Ctrl+C to stop." << std::endl;
    } else {
        std::cout << "[tello_cli] Single-run smoke mode starting..." << std::endl;
    }

    tello::TelloClient client;

    // Step 1: open command socket to Tello default endpoint.
    tello::ResponseCode init_rc = client.initialize("192.168.10.1", 8889, 8889);
    std::cout << "[tello_cli] initialize: " << responseCodeToString(init_rc) << std::endl;

    if (init_rc != tello::ResponseCode::OK) {
        std::cout << "[tello_cli] Cannot continue without initialization." << std::endl;
        return 1;
    }

    int attempt = 0;
    const std::string run_mode_str = runModeToString(options.mode);

    if (options.mode == RunMode::VIDEO_WATCH) {
        std::ofstream metrics_csv;
        const bool metrics_enabled = !options.metrics_csv_path.empty();
        tello::MetricsCollector metrics;
        tello::MetricsCollector::ExperimentMetadata metadata{};
        metadata.test_id = options.test_id;
        metadata.scenario = options.scenario;
        metadata.notes = options.notes;
        metadata.run_mode = run_mode_str;
        metrics.setExperimentMetadata(metadata);

        if (metrics_enabled) {
            metrics_csv.open(options.metrics_csv_path, std::ios::out | std::ios::trunc);
            if (!metrics_csv.is_open()) {
                std::cerr << "[tello_cli] failed to open metrics csv: " << options.metrics_csv_path << std::endl;
                client.shutdown();
                return 6;
            }
            metrics_csv << metrics.toCsvHeader() << std::endl;
            std::cout << "[tello_cli] metrics_csv: " << options.metrics_csv_path << std::endl;
        }

        const tello::ResponseCode sdk_rc = client.enterSdkMode();
        std::cout << "[tello_cli] enterSdkMode(command): " << responseCodeToString(sdk_rc) << std::endl;
        if (sdk_rc != tello::ResponseCode::OK) {
            client.shutdown();
            return 3;
        }

        const tello::ResponseCode keepalive_rc = client.startSdkKeepalive(5000);
        std::cout << "[tello_cli] sdk_keepalive: " << responseCodeToString(keepalive_rc)
                  << " (battery? every 5000 ms)" << std::endl;

        const tello::ResponseCode stream_on_rc = client.streamOn();
        std::cout << "[tello_cli] streamOn: " << responseCodeToString(stream_on_rc) << std::endl;
        if (stream_on_rc != tello::ResponseCode::OK) {
            client.shutdown();
            return 4;
        }

        std::cout << "[tello_cli] video_backend: FFmpeg Stream" << std::endl;

        tello::VideoStreamReaderFfmpeg stream_reader;
        const std::string stream_url = "udp://@0.0.0.0:11111?overrun_nonfatal=1&fifo_size=5000000";
        const tello::ResponseCode start_rc = stream_reader.start(stream_url);
        std::cout << "[tello_cli] ffmpeg_stream_reader.start: " << responseCodeToString(start_rc) << std::endl;
        if (start_rc != tello::ResponseCode::OK) {
            (void)client.streamOff();
            client.shutdown();
            return 5;
        }

        tello::StateReceiver state_receiver;
        const tello::ResponseCode state_start_rc = state_receiver.start("0.0.0.0", 8890, 500);
        std::cout << "[tello_cli] state_receiver.start: " << responseCodeToString(state_start_rc) << std::endl;
        if (state_start_rc != tello::ResponseCode::OK) {
            stream_reader.stop();
            (void)client.streamOff();
            client.shutdown();
            return 5;
        }

        uint64_t last_count = 0;
        const int64_t stall_age_threshold_ms = std::max<int64_t>(3000, static_cast<int64_t>(options.interval_ms) * 3);
        const int64_t recovery_cooldown_ms = 3000;

        while (g_keep_running.load()) {
            if (shouldStopByDuration()) {
                std::cout << "[tello_cli] duration limit reached, stopping." << std::endl;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
            ++attempt;

            const tello::VideoStreamReaderFfmpeg::Stats stream_stats = stream_reader.getStats();
            const tello::StateReceiver::TelemetryStats telemetry_stats = state_receiver.getTelemetryStats();
            const bool telemetry_available = state_receiver.hasReceivedState();
            const tello::TelloState telemetry_state =
                telemetry_available ? state_receiver.getLatestState() : tello::TelloState{};
            const uint64_t count = stream_stats.packets_read;
            const uint64_t delta = count - last_count;

            std::cout << "[tello_cli] attempt=" << attempt
                      << " backend=ffmpeg_stream"
                      << " packets_read=" << count
                      << " delta=" << delta
                      << " bytes_read=" << stream_stats.bytes_read
                      << " frame_age_ms=" << stream_stats.last_frame_age_ms
                      << " frames=" << stream_stats.frames_decoded
                      << " decode_fps=" << stream_stats.decode_fps_ema
                      << " decode_errors=" << stream_stats.decode_errors
                      << " frame_size=" << stream_stats.frame_width << "x" << stream_stats.frame_height
                      << " keyframes=" << stream_stats.keyframes
                      << " | state_age_ms=" << telemetry_stats.last_packet_age_ms
                      << " state_hz=" << telemetry_stats.rx_hz_ema;
            if (telemetry_available) {
                std::cout << " bat=" << telemetry_state.bat
                          << " templ=" << telemetry_state.templ
                          << " temph=" << telemetry_state.temph
                          << " h=" << telemetry_state.h
                          << " tof=" << telemetry_state.tof;
            } else {
                std::cout << " state=waiting";
            }
            std::cout << std::endl;

            const std::string err = stream_reader.getLastError();
            if (!err.empty()) {
                std::cout << "[tello_cli] ffmpeg_stream_last_error=\"" << err << "\"" << std::endl;
            }

            tello::TelloClient::VideoRecoveryStatus recovery =
                client.recoverVideoStreamIfStalled(
                    stream_stats.last_frame_age_ms,
                    stall_age_threshold_ms,
                    recovery_cooldown_ms);
            bool power_cycle_recovery_used = false;
            if (recovery.attempted
                && recovery.stage == "battery_probe"
                && !recovery.command_channel_available) {
                std::cout << "[tello_cli] command channel unavailable; attempting power-cycle recovery..." << std::endl;
                recovery = client.recoverAfterPowerCycle();
                power_cycle_recovery_used = true;
            }

            bool video_pipeline_restarted = false;
            tello::ResponseCode video_restart_rc = tello::ResponseCode::OK;
            if (shouldRestartLocalVideoPipeline(recovery)) {
                stream_reader.stop();
                video_restart_rc = stream_reader.start(stream_url);
                video_pipeline_restarted = (video_restart_rc == tello::ResponseCode::OK);
                if (video_pipeline_restarted) {
                    last_count = 0;
                    std::cout << "[tello_cli] ffmpeg stream reader restart: OK" << std::endl;
                } else {
                    std::cout << "[tello_cli] ffmpeg stream reader restart: "
                              << responseCodeToString(video_restart_rc) << std::endl;
                }
            }

            tello::TelloClient::ConnectionEvent event = tello::TelloClient::ConnectionEvent::NONE;

            if (recovery.attempted) {
                std::cout << "[tello_cli] video stall detected (age_ms=" << stream_stats.last_frame_age_ms
                          << "), attempting stream recovery..." << std::endl;
                std::cout << "[tello_cli] streamOn(recover): " << responseCodeToString(recovery.result)
                          << " | stage=" << recovery.stage
                          << " | command_channel=" << (recovery.command_channel_available ? "available" : "unavailable")
                          << " | hard_recovery=" << (recovery.used_hard_recovery ? "yes" : "no")
                          << " | video_restart=" << (video_pipeline_restarted ? "yes" : "no")
                          << " | cmd_state=" << connectionStateToString(client.getConnectionState())
                          << std::endl;

                event = client.consumeConnectionEvent();
                printConnectionEvent(client, event);
            }

            if (metrics_enabled) {
                const auto elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - run_started_at).count();

                std::string event_str = connectionEventToString(event);
                if (event == tello::TelloClient::ConnectionEvent::NONE && recovery.attempted) {
                    event_str = power_cycle_recovery_used ? "VIDEO_POWER_RECOVERY" : "VIDEO_RECOVERY";
                }

                tello::MetricsCollector::VideoTransportStats rx_stats{};
                rx_stats.packets_total = stream_stats.packets_read;
                rx_stats.bytes_total = stream_stats.bytes_read;
                rx_stats.last_packet_age_ms = stream_stats.last_frame_age_ms;
                rx_stats.rx_pps_ema = stream_stats.decode_fps_ema;

                tello::MetricsCollector::VideoDecodeStats decoder_stats{};
                decoder_stats.frames_decoded = stream_stats.frames_decoded;
                decoder_stats.decode_errors = stream_stats.decode_errors;
                decoder_stats.decode_fps_ema = stream_stats.decode_fps_ema;
                decoder_stats.frame_width = stream_stats.frame_width;
                decoder_stats.frame_height = stream_stats.frame_height;
                decoder_stats.keyframes = stream_stats.keyframes;

                metrics.setElapsedMs(elapsed_ms);
                metrics.setAttempt(static_cast<uint64_t>(attempt));
                metrics.updateTelemetryStats(telemetry_stats);
                metrics.updateTelemetryState(telemetry_state, telemetry_available);
                metrics.updateVideoTransportStats(rx_stats);
                metrics.setVideoPacketDelta(delta);
                metrics.updateDecoderStats(decoder_stats);
                metrics.recordRecoveryEvent(
                    recovery.attempted,
                    recovery.result,
                    recovery.used_hard_recovery,
                    recovery.stage,
                    recovery.command_channel_available);
                metrics.updateRuntimeContext(
                    false,
                    0,
                    0,
                    0,
                    0,
                    client.isSdkKeepaliveRunning(),
                    false,
                    false);
                const auto keepalive_stats = client.getSdkKeepaliveStats();
                metrics.updateKeepaliveStats(
                    keepalive_stats.tick_total,
                    keepalive_stats.success_total,
                    keepalive_stats.failure_total,
                    keepalive_stats.skipped_busy_total,
                    keepalive_stats.skipped_uninitialized_total,
                    keepalive_stats.last_command,
                    keepalive_stats.last_response,
                    keepalive_stats.last_result,
                    keepalive_stats.last_latency_ms,
                    keepalive_stats.last_success_age_ms,
                    keepalive_stats.last_tick_age_ms);
                metrics.setEvent(event_str);
                metrics.setConnectionState(connectionStateToString(client.getConnectionState()));
                metrics.setLastOutageFailures(client.getLastOutageFailures());
                metrics_csv << metrics.toCsvLine() << std::endl;
            }

            last_count = count;
        }

        state_receiver.stop();
        stream_reader.stop();
        const tello::ResponseCode stream_off_rc = client.streamOff();
        std::cout << "[tello_cli] streamOff: " << responseCodeToString(stream_off_rc) << std::endl;
        client.shutdown();
        std::cout << "[tello_cli] Stopped." << std::endl;
        return 0;
    }

    if (options.mode == RunMode::STATE_WATCH) {
        std::ofstream metrics_csv;
        const bool metrics_enabled = !options.metrics_csv_path.empty();
        tello::MetricsCollector metrics;
        tello::MetricsCollector::ExperimentMetadata metadata{};
        metadata.test_id = options.test_id;
        metadata.scenario = options.scenario;
        metadata.notes = options.notes;
        metadata.run_mode = run_mode_str;
        metrics.setExperimentMetadata(metadata);

        if (metrics_enabled) {
            metrics_csv.open(options.metrics_csv_path, std::ios::out | std::ios::trunc);
            if (!metrics_csv.is_open()) {
                std::cerr << "[tello_cli] failed to open metrics csv: " << options.metrics_csv_path << std::endl;
                client.shutdown();
                return 6;
            }
            metrics_csv << metrics.toCsvHeader() << std::endl;
            std::cout << "[tello_cli] metrics_csv: " << options.metrics_csv_path << std::endl;
        }

        // Entering SDK mode once improves chance of receiving telemetry state packets.
        const tello::ResponseCode sdk_rc = client.enterSdkMode();
        std::cout << "[tello_cli] enterSdkMode(command): " << responseCodeToString(sdk_rc) << std::endl;
        if (sdk_rc == tello::ResponseCode::OK) {
            const tello::ResponseCode keepalive_rc = client.startSdkKeepalive(5000);
            std::cout << "[tello_cli] sdk_keepalive: " << responseCodeToString(keepalive_rc)
                      << " (battery? every 5000 ms)" << std::endl;
        }

        tello::StateReceiver receiver;
        const tello::ResponseCode state_start_rc = receiver.start("0.0.0.0", 8890, 500);
        std::cout << "[tello_cli] state_receiver.start: " << responseCodeToString(state_start_rc) << std::endl;
        if (state_start_rc != tello::ResponseCode::OK) {
            client.shutdown();
            return 3;
        }

        while (g_keep_running.load()) {
            if (shouldStopByDuration()) {
                std::cout << "[tello_cli] duration limit reached, stopping." << std::endl;
                break;
            }

            ++attempt;

            const tello::StateReceiver::TelemetryStats stats = receiver.getTelemetryStats();
            const tello::TelloClient::ConnectionEvent event = client.consumeConnectionEvent();

            printConnectionEvent(client, event);

            constexpr int64_t kCliStateStaleThresholdMs = 1000;
            if (!receiver.hasReceivedState()) {
                std::cout << "[tello_cli] attempt=" << attempt
                          << " state: waiting packets..."
                          << " | rx_hz=" << stats.rx_hz_ema
                          << " | age_ms=" << stats.last_packet_age_ms
                          << std::endl;
            } else {
                const tello::TelloState s = receiver.getLatestState();
                std::cout << "[tello_cli] attempt=" << attempt
                          << (stats.last_packet_age_ms > kCliStateStaleThresholdMs
                                  ? " state: STALE latest"
                                  : " state:")
                          << " bat=" << s.bat
                          << " h=" << s.h
                          << " pitch=" << s.pitch
                          << " roll=" << s.roll
                          << " yaw=" << s.yaw
                          << " vgx=" << s.vgx
                          << " vgy=" << s.vgy
                          << " vgz=" << s.vgz
                          << " | rx_hz=" << stats.rx_hz_ema
                          << " | age_ms=" << stats.last_packet_age_ms
                          << std::endl;
            }

            if (metrics_enabled) {
                const auto elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - run_started_at).count();

                std::string event_str = connectionEventToString(event);

                metrics.setElapsedMs(elapsed_ms);
                metrics.setAttempt(static_cast<uint64_t>(attempt));
                metrics.updateTelemetryStats(stats);
                metrics.updateRuntimeContext(
                    false,
                    0,
                    0,
                    0,
                    0,
                    client.isSdkKeepaliveRunning(),
                    false,
                    false);
                const auto keepalive_stats = client.getSdkKeepaliveStats();
                metrics.updateKeepaliveStats(
                    keepalive_stats.tick_total,
                    keepalive_stats.success_total,
                    keepalive_stats.failure_total,
                    keepalive_stats.skipped_busy_total,
                    keepalive_stats.skipped_uninitialized_total,
                    keepalive_stats.last_command,
                    keepalive_stats.last_response,
                    keepalive_stats.last_result,
                    keepalive_stats.last_latency_ms,
                    keepalive_stats.last_success_age_ms,
                    keepalive_stats.last_tick_age_ms);
                metrics.setConnectionState(connectionStateToString(client.getConnectionState()));
                metrics.setEvent(event_str);
                metrics.setLastOutageFailures(client.getLastOutageFailures());
                metrics_csv << metrics.toCsvLine() << std::endl;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
        }

        receiver.stop();
        client.shutdown();
        std::cout << "[tello_cli] Stopped." << std::endl;
        return 0;
    }

    std::ofstream metrics_csv;
    const bool metrics_enabled = !options.metrics_csv_path.empty();
    tello::MetricsCollector metrics;
    tello::MetricsCollector::ExperimentMetadata metadata{};
    metadata.test_id = options.test_id;
    metadata.scenario = options.scenario;
    metadata.notes = options.notes;
    metadata.run_mode = run_mode_str;
    metrics.setExperimentMetadata(metadata);

    if (metrics_enabled) {
        metrics_csv.open(options.metrics_csv_path, std::ios::out | std::ios::trunc);
        if (!metrics_csv.is_open()) {
            std::cerr << "[tello_cli] failed to open metrics csv: " << options.metrics_csv_path << std::endl;
            client.shutdown();
            return 6;
        }
        metrics_csv << metrics.toCsvHeader() << std::endl;
        std::cout << "[tello_cli] metrics_csv: " << options.metrics_csv_path << std::endl;
    }

    do {
        if (!g_keep_running.load()) {
            break;
        }

        if (shouldStopByDuration()) {
            std::cout << "[tello_cli] duration limit reached, stopping." << std::endl;
            break;
        }

        ++attempt;

        std::string battery_response;
        const auto command_started_at = std::chrono::steady_clock::now();
        const tello::ResponseCode battery_rc = client.getBattery(battery_response);
        const auto command_elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - command_started_at).count();
        std::cout << "[tello_cli] attempt=" << attempt
                  << " battery?: " << responseCodeToString(battery_rc);

        if (!battery_response.empty()) {
            std::cout << " | response=\"" << battery_response << "\"";
        }
        std::cout << " | state=" << connectionStateToString(client.getConnectionState());
        std::cout << std::endl;

        const tello::TelloClient::ConnectionEvent event = client.consumeConnectionEvent();
        printConnectionEvent(client, event);

        if (metrics_enabled) {
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - run_started_at).count();

            std::string event_str = connectionEventToString(event);

            metrics.setElapsedMs(elapsed_ms);
            metrics.setAttempt(static_cast<uint64_t>(attempt));
            metrics.recordCommandResult(
                "battery?",
                static_cast<double>(command_elapsed_ms),
                battery_rc,
                battery_response);
            if (const auto executor = client.getCommandExecutor()) {
                const auto timing = executor->getLastTimingStats();
                metrics.updateCommandDiagnostics(
                    "cli-watch",
                    executor->getLastError(),
                    executor->getLastAttemptLog(),
                    client.getLastCommandInternalAttemptLog());
                metrics.updateCommandTimingDiagnostics(
                    client.getLastCommandClientTotalMs(),
                    client.getLastEnsureSdkModeMs(),
                    client.getLastCommandExecutorMs(),
                    timing.send_ms,
                    timing.recv_wait_ms,
                    timing.parse_ms,
                    client.getLastCommandExecutorRecvWaitTotalMs(),
                    client.getLastCommandExecutorInternalTotalMs(),
                    client.getLastCommandRecoveryMs(),
                    client.getLastCommandExecutorCalls(),
                    client.getLastCommandRecoveryCount());
            }
            metrics.setConnectionState(connectionStateToString(client.getConnectionState()));
            metrics.setEvent(event_str);
            metrics.setLastOutageFailures(client.getLastOutageFailures());
            metrics_csv << metrics.toCsvLine() << std::endl;
        }

        if (options.mode == RunMode::WATCH) {
            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
        }
    } while (options.mode == RunMode::WATCH);

    client.shutdown();
    std::cout << "[tello_cli] Stopped." << std::endl;
    return 0;
}
