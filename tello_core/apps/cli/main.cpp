#include "tello/tello_client.hpp"
#include "tello/logger.hpp"
#include "tello/metrics.hpp"
#include "tello/state_receiver.hpp"
#include "tello/video_decoder_ffmpeg.hpp"
#include "tello/video_pipeline_recovery.hpp"
#include "tello/video_receiver.hpp"
#include "tello/video_stream_assembler.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <csignal>
#include <fstream>
#include <iostream>
#include <memory>
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

    auto logger = std::make_shared<tello::ConsoleLogger>(tello::Logger::Level::DEBUG);
    tello::initializeGlobalLogger(logger);

    tello::TelloClient client;

    // Step 1: open command socket to Tello default endpoint.
    tello::ResponseCode init_rc = client.initialize("192.168.10.1", 8889, 9000);
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

        const tello::ResponseCode stream_on_rc = client.streamOn();
        std::cout << "[tello_cli] streamOn: " << responseCodeToString(stream_on_rc) << std::endl;
        if (stream_on_rc != tello::ResponseCode::OK) {
            client.shutdown();
            return 4;
        }

        tello::VideoReceiver receiver;
        tello::VideoStreamAssembler assembler;
    #ifdef TELLO_HAS_FFMPEG
        tello::VideoDecoderFfmpeg decoder;
        bool decoder_ready = decoder.initialize();
        std::cout << "[tello_cli] ffmpeg_decoder: " << (decoder_ready ? "enabled" : "init_failed") << std::endl;
    #else
        std::cout << "[tello_cli] ffmpeg_decoder: not_built" << std::endl;
    #endif
        std::atomic<uint64_t> nal_sps{0};
        std::atomic<uint64_t> nal_pps{0};
        std::atomic<uint64_t> nal_idr{0};
        std::atomic<uint64_t> nal_non_idr{0};
        std::atomic<uint64_t> nal_other{0};
        std::atomic<uint64_t> nal_decode_gated{0};
        std::atomic<bool> decoder_seen_sps{false};
        std::atomic<bool> decoder_seen_pps{false};
        std::atomic<bool> decoder_synced{false};

        assembler.setNalCallback([&](const std::vector<uint8_t>& nal) {
            if (nal.empty()) {
                return;
            }

            const uint8_t nal_type = static_cast<uint8_t>(nal[0] & 0x1F);
            switch (nal_type) {
                case 7:
                    ++nal_sps;
                    decoder_seen_sps = true;
                    break;
                case 8:
                    ++nal_pps;
                    decoder_seen_pps = true;
                    break;
                case 5:
                    ++nal_idr;
                    if (decoder_seen_sps.load() && decoder_seen_pps.load()) {
                        decoder_synced = true;
                    }
                    break;
                case 1:
                    ++nal_non_idr;
                    break;
                default:
                    ++nal_other;
                    break;
            }

#ifdef TELLO_HAS_FFMPEG
            if (decoder_ready) {
                const bool is_parameter_set = (nal_type == 7 || nal_type == 8);
                const bool is_idr = (nal_type == 5);
                const bool allow_decode = is_parameter_set || is_idr || decoder_synced.load();
                if (allow_decode) {
                    (void)decoder.decodeNal(nal);
                } else {
                    ++nal_decode_gated;
                }
            }
#endif
        });

        receiver.setPacketCallback([&assembler](const std::vector<uint8_t>& packet) {
            assembler.pushPacket(packet);
        });
        const tello::ResponseCode start_rc = receiver.start("0.0.0.0", 11111, 500);
        std::cout << "[tello_cli] video_receiver.start: " << responseCodeToString(start_rc) << std::endl;
        if (start_rc != tello::ResponseCode::OK) {
            (void)client.streamOff();
            client.shutdown();
            return 5;
        }

        uint64_t last_count = 0;
        const int64_t stall_age_threshold_ms = std::max<int64_t>(3000, static_cast<int64_t>(options.interval_ms) * 3);
        const int64_t recovery_cooldown_ms = 3000;
        tello::VideoPipelineRecoveryOptions video_recovery_options;
        video_recovery_options.bind_ip = "0.0.0.0";
        video_recovery_options.video_port = 11111;
        video_recovery_options.receiver_timeout_ms = 500;
        tello::VideoPipelineRecoveryHooks video_recovery_hooks;
        video_recovery_hooks.before_restart = [&]() {
            nal_sps = 0;
            nal_pps = 0;
            nal_idr = 0;
            nal_non_idr = 0;
            nal_other = 0;
            nal_decode_gated = 0;
            decoder_seen_sps = false;
            decoder_seen_pps = false;
            decoder_synced = false;
        };

        while (g_keep_running.load()) {
            if (shouldStopByDuration()) {
                std::cout << "[tello_cli] duration limit reached, stopping." << std::endl;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(options.interval_ms));
            ++attempt;

            const tello::VideoReceiver::VideoStats stats = receiver.getVideoStats();
            const tello::VideoStreamAssembler::Stats assembler_stats = assembler.getStats();
            const uint64_t count = stats.packets_total;
            const uint64_t delta = count - last_count;

#ifdef TELLO_HAS_FFMPEG
            const tello::VideoDecoderFfmpeg::Stats decoder_stats = decoder.getStats();
#endif

            std::cout << "[tello_cli] attempt=" << attempt
                      << " packets_total=" << count
                      << " delta=" << delta
                      << " pps=" << stats.rx_pps_ema
                      << " bytes_total=" << stats.bytes_total
                      << " age_ms=" << stats.last_packet_age_ms
                      << " timeouts=" << stats.recv_timeouts
                      << " errors=" << stats.recv_errors
                      << " nal_units=" << assembler_stats.nal_units_out
                      << " nal_sps=" << nal_sps.load()
                      << " nal_pps=" << nal_pps.load()
                      << " nal_idr=" << nal_idr.load()
                      << " nal_nonidr=" << nal_non_idr.load()
                      << " nal_other=" << nal_other.load()
                      << " nal_gated=" << nal_decode_gated.load()
                      << " parser_resyncs=" << assembler_stats.parse_resyncs
                      << " buffered_bytes=" << assembler_stats.buffered_bytes
#ifdef TELLO_HAS_FFMPEG
                      << " dec_frames=" << decoder_stats.frames_decoded
                      << " dec_fps=" << decoder_stats.decode_fps_ema
                      << " dec_errors=" << decoder_stats.decode_errors
#endif
                      << std::endl;

            const std::string err = receiver.getLastError();
            if (!err.empty()) {
                std::cout << "[tello_cli] video_last_error=\"" << err << "\"" << std::endl;
            }

            const tello::VideoPipelineRecoveryResult recovery_result =
                tello::VideoPipelineRecovery::recoverIfStalled(
                    client,
                    receiver,
                    assembler,
                    stats.last_packet_age_ms,
                    stall_age_threshold_ms,
                    recovery_cooldown_ms,
                    video_recovery_options,
                    video_recovery_hooks
#ifdef TELLO_HAS_FFMPEG
                    ,
                    &decoder,
                    &decoder_ready
#endif
                );
            const tello::TelloClient::VideoRecoveryStatus recovery = recovery_result.session;
            const bool video_pipeline_restarted = recovery_result.video_pipeline_restarted;
            const tello::ResponseCode video_restart_rc = recovery_result.video_restart_result;

            if (recovery_result.power_cycle_recovery_used) {
                std::cout << "[tello_cli] command channel unavailable; attempting power-cycle recovery..." << std::endl;
            }

            if (recovery.attempted && video_restart_rc != tello::ResponseCode::OK) {
                std::cout << "[tello_cli] video pipeline restart: "
                          << responseCodeToString(video_restart_rc) << std::endl;
            } else if (video_pipeline_restarted) {
                last_count = 0;
                std::cout << "[tello_cli] video pipeline restart: OK" << std::endl;
            }

            tello::TelloClient::ConnectionEvent event = tello::TelloClient::ConnectionEvent::NONE;

            if (recovery.attempted) {
                std::cout << "[tello_cli] video stall detected (age_ms=" << stats.last_packet_age_ms
                          << "), attempting stream recovery..." << std::endl;
                std::cout << "[tello_cli] streamOn(recover): " << responseCodeToString(recovery.result)
                          << " | stage=" << recovery.stage
                          << " | command_channel=" << (recovery.command_channel_available ? "available" : "unavailable")
                          << " | hard_recovery=" << (recovery.used_hard_recovery ? "yes" : "no")
                          << " | video_restart=" << (video_pipeline_restarted ? "yes" : "no")
                          << " | cmd_state=" << connectionStateToString(client.getConnectionState())
                          << std::endl;

                event = client.consumeConnectionEvent();
                if (event == tello::TelloClient::ConnectionEvent::LOST) {
                    std::cout << "[tello_cli] event: connection lost" << std::endl;
                } else if (event == tello::TelloClient::ConnectionEvent::RESTORED) {
                    std::cout << "[tello_cli] event: connection restored"
                              << " (after " << client.getLastOutageFailures() << " failed attempts)"
                              << std::endl;
                }
            }

            if (metrics_enabled) {
                const auto elapsed_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - run_started_at).count();

                std::string event_str = "NONE";
                if (event == tello::TelloClient::ConnectionEvent::LOST) {
                    event_str = "LOST";
                } else if (event == tello::TelloClient::ConnectionEvent::RESTORED) {
                    event_str = "RESTORED";
                }

                metrics.setElapsedMs(elapsed_ms);
                metrics.setAttempt(static_cast<uint64_t>(attempt));
                metrics.updateVideoReceiverStats(stats);
                metrics.setVideoPacketDelta(delta);
                metrics.updateVideoAssemblerStats(assembler_stats);
                metrics.updateNalClassificationStats(
                    nal_sps.load(),
                    nal_pps.load(),
                    nal_idr.load(),
                    nal_non_idr.load(),
                    nal_other.load(),
                    nal_decode_gated.load());
#ifdef TELLO_HAS_FFMPEG
                metrics.updateDecoderStats(decoder_stats);
#else
                metrics.updateDecoderStats(tello::VideoDecoderFfmpeg::Stats{});
#endif
                metrics.recordRecoveryEvent(
                    recovery.attempted,
                    recovery.result,
                    recovery.used_hard_recovery,
                    recovery.stage,
                    recovery.command_channel_available);
                metrics.setEvent(event_str);
                metrics.setConnectionState(connectionStateToString(client.getConnectionState()));
                metrics.setLastOutageFailures(client.getLastOutageFailures());
                metrics_csv << metrics.toCsvLine() << std::endl;
            }

            last_count = count;
        }

        receiver.stop();
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

            if (event == tello::TelloClient::ConnectionEvent::LOST) {
                std::cout << "[tello_cli] event: connection lost" << std::endl;
            } else if (event == tello::TelloClient::ConnectionEvent::RESTORED) {
                std::cout << "[tello_cli] event: connection restored"
                          << " (after " << client.getLastOutageFailures() << " failed attempts)"
                          << std::endl;
            }

            if (!receiver.hasReceivedState()) {
                std::cout << "[tello_cli] attempt=" << attempt
                          << " state: waiting packets..."
                          << " | rx_hz=" << stats.rx_hz_ema
                          << " | age_ms=" << stats.last_packet_age_ms
                          << std::endl;
            } else {
                const tello::TelloState s = receiver.getLatestState();
                std::cout << "[tello_cli] attempt=" << attempt
                          << " state:"
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

                std::string event_str = "NONE";
                if (event == tello::TelloClient::ConnectionEvent::LOST) {
                    event_str = "LOST";
                } else if (event == tello::TelloClient::ConnectionEvent::RESTORED) {
                    event_str = "RESTORED";
                }

                metrics.setElapsedMs(elapsed_ms);
                metrics.setAttempt(static_cast<uint64_t>(attempt));
                metrics.updateTelemetryStats(stats);
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
        if (event == tello::TelloClient::ConnectionEvent::LOST) {
            std::cout << "[tello_cli] event: connection lost" << std::endl;
        } else if (event == tello::TelloClient::ConnectionEvent::RESTORED) {
            std::cout << "[tello_cli] event: connection restored"
                      << " (after " << client.getLastOutageFailures() << " failed attempts)"
                      << std::endl;
        }

        if (metrics_enabled) {
            const auto elapsed_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - run_started_at).count();

            std::string event_str = "NONE";
            if (event == tello::TelloClient::ConnectionEvent::LOST) {
                event_str = "LOST";
            } else if (event == tello::TelloClient::ConnectionEvent::RESTORED) {
                event_str = "RESTORED";
            }

            metrics.setElapsedMs(elapsed_ms);
            metrics.setAttempt(static_cast<uint64_t>(attempt));
            metrics.recordCommandResult(
                "battery?",
                static_cast<double>(command_elapsed_ms),
                battery_rc,
                battery_response);
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
