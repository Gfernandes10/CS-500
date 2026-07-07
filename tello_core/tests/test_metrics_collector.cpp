#include "tello/metrics.hpp"

#include <cmath>
#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[test_metrics_collector] FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

bool expectNear(double actual, double expected, const std::string& message) {
    return expect(std::fabs(actual - expected) < 0.0001, message);
}

size_t countCsvColumns(const std::string& row) {
    bool in_quotes = false;
    size_t columns = 1;

    for (size_t i = 0; i < row.size(); ++i) {
        const char c = row[i];
        if (c == '"') {
            if (in_quotes && i + 1 < row.size() && row[i + 1] == '"') {
                ++i;
            } else {
                in_quotes = !in_quotes;
            }
        } else if (c == ',' && !in_quotes) {
            ++columns;
        }
    }

    return row.empty() ? 0 : columns;
}

} // namespace

int main() {
    tello::MetricsCollector metrics;
    bool ok = true;

    // Case 1: command result aggregation tracks counts and latency stats.
    metrics.recordCommandResult("command", 120.0, tello::ResponseCode::OK, "ok");
    metrics.recordCommandResult("battery?", 80.0, tello::ResponseCode::OK, "87");
    metrics.recordCommandResult("speed?", 200.0, tello::ResponseCode::TIMEOUT, "");

    auto snapshot = metrics.getSnapshot();
    ok &= expect(snapshot.command_samples == 3, "command sample count should be 3");
    ok &= expect(snapshot.command_successes == 2, "command success count should be 2");
    ok &= expect(snapshot.command_failures == 1, "command failure count should be 1");
    ok &= expectNear(snapshot.command_latency_ms_avg, 400.0 / 3.0, "average command latency should match");
    ok &= expectNear(snapshot.command_latency_ms_min, 80.0, "minimum command latency should match");
    ok &= expectNear(snapshot.command_latency_ms_max, 200.0, "maximum command latency should match");
    ok &= expectNear(snapshot.last_command_latency_ms, 200.0, "last command latency should match");
    ok &= expect(snapshot.last_command == "speed?", "last command should be preserved");
    ok &= expect(snapshot.last_command_result == tello::ResponseCode::TIMEOUT, "last command result should be preserved");
    metrics.updateCommandDiagnostics(
        "unit",
        "Timeout waiting for response",
        "attempt=1|send=OK|raw=87|parsed=OK",
        "executor_call=1|command=battery?|result=TIMEOUT|attempt_log=[attempt=1|send=OK|raw=|result=TIMEOUT];"
        "recovery_call=1|result=OK|elapsed_ms=20;"
        "executor_call=2|command=battery?|result=OK|attempt_log=[attempt=1|send=OK|raw=87|parsed=OK]");
    snapshot = metrics.getSnapshot();
    ok &= expect(snapshot.command_source == "unit", "command source should be stored");
    ok &= expect(snapshot.command_attempt_count == 2, "attempt count should be parsed from attempt log");
    ok &= expect(snapshot.command_retry_count == 1, "retry count should be attempts minus one");
    ok &= expect(snapshot.command_timeout_count == 1,
                 "timeout count should be parsed from attempt log, got "
                     + std::to_string(snapshot.command_timeout_count));
    ok &= expect(snapshot.command_internal_attempt_log.find("recovery_call=1") != std::string::npos,
                 "internal command attempt log should be stored");
    metrics.updateCommandTimingDiagnostics(250, 5, 240, 1, 237, 2, 237, 238, 10, 1, 0);
    snapshot = metrics.getSnapshot();
    ok &= expect(snapshot.command_client_total_ms == 250, "client command total timing should be stored");
    ok &= expect(snapshot.command_ensure_sdk_ms == 5, "ensure SDK timing should be stored");
    ok &= expect(snapshot.command_executor_total_ms == 240, "executor timing should be stored");
    ok &= expect(snapshot.command_send_ms == 1, "send timing should be stored");
    ok &= expect(snapshot.command_recv_wait_ms == 237, "receive wait timing should be stored");
    ok &= expect(snapshot.command_parse_ms == 2, "parse timing should be stored");
    ok &= expect(snapshot.command_executor_recv_wait_total_ms == 237, "aggregate receive wait timing should be stored");
    ok &= expect(snapshot.command_executor_internal_total_ms == 238, "aggregate executor internal timing should be stored");
    ok &= expect(snapshot.command_recovery_ms == 10, "recovery timing should be stored");
    ok &= expect(snapshot.command_executor_calls == 1, "executor call count should be stored");
    ok &= expect(snapshot.command_recovery_count == 0, "recovery count should be stored");

    // Case 2: subsystem snapshots are copied into the central metrics snapshot.
    tello::StateReceiver::TelemetryStats telemetry{};
    telemetry.packets_total = 10;
    telemetry.packets_valid = 9;
    telemetry.packets_invalid = 1;
    telemetry.recv_timeouts = 2;
    telemetry.rx_hz_ema = 29.5;
    telemetry.last_packet_age_ms = 15;
    telemetry.last_interarrival_ms = 33;
    metrics.updateTelemetryStats(telemetry);

    tello::MetricsCollector::VideoTransportStats video{};
    video.packets_total = 100;
    video.bytes_total = 4096;
    video.recv_errors = 3;
    video.rx_pps_ema = 120.25;
    video.last_packet_age_ms = 10;
    video.last_interarrival_ms = 4;
    metrics.updateVideoTransportStats(video);
    metrics.setVideoPacketDelta(12);

    tello::MetricsCollector::VideoAssemblyStats nal{};
    nal.packets_in = 100;
    nal.nal_units_out = 42;
    nal.buffered_bytes = 7;
    metrics.updateVideoAssemblerStats(nal);
    metrics.updateNalClassificationStats(2, 2, 1, 37, 0, 4);

    tello::MetricsCollector::VideoDecodeStats decoder{};
    decoder.nals_in = 42;
    decoder.frames_decoded = 30;
    decoder.decode_errors = 1;
    decoder.decode_fps_ema = 28.75;
    metrics.updateDecoderStats(decoder);

    metrics.updateFrameInfo(960, 720, true);
    metrics.updateDisplayState(true, false);
    metrics.updatePanelDiagnostics("tof", 3, true);
    metrics.updateGuiPerformance(120, 250, 5, 3, 2, 4, 1, 0, 1, 6, 8, 24, 7, 120);
    metrics.recordRecoveryEvent(true, tello::ResponseCode::OK, true);
    metrics.setConnectionState("CONNECTED");
    metrics.setEvent("stream_restored");
    metrics.setAttempt(4);
    metrics.setLastOutageFailures(2);

    snapshot = metrics.getSnapshot();
    ok &= expect(snapshot.telemetry.packets_total == 10, "telemetry stats should be copied");
    ok &= expect(snapshot.telemetry_quality == "OK", "fresh telemetry should be marked OK");
    ok &= expect(snapshot.telemetry_quality_score == 100, "fresh telemetry quality score should be 100");
    ok &= expect(snapshot.video_rx.bytes_total == 4096, "video receiver stats should be copied");
    ok &= expect(snapshot.video_quality == "OK", "fresh video should be marked OK");
    ok &= expect(snapshot.video_quality_score == 100, "fresh video quality score should be 100");
    ok &= expect(snapshot.video_packet_delta == 12, "video packet delta should be stored");
    ok &= expect(snapshot.nal.nal_units_out == 42, "NAL stats should be copied");
    ok &= expect(snapshot.command_mutex_wait_ms == 6, "command mutex wait should be stored");
    ok &= expect(snapshot.nal_sps == 2, "NAL SPS count should be stored");
    ok &= expect(snapshot.nal_pps == 2, "NAL PPS count should be stored");
    ok &= expect(snapshot.nal_idr == 1, "NAL IDR count should be stored");
    ok &= expect(snapshot.nal_non_idr == 37, "NAL non-IDR count should be stored");
    ok &= expect(snapshot.nal_decode_gated == 4, "NAL gated count should be stored");
    ok &= expect(snapshot.decoder.frames_decoded == 30, "decoder stats should be copied");
    ok &= expect(snapshot.frame_width == 960 && snapshot.frame_height == 720, "frame size should be stored");
    ok &= expect(snapshot.keyframes == 1, "keyframe count should increment");
    ok &= expect(snapshot.paused, "paused display state should be stored");
    ok &= expect(!snapshot.overlay_enabled, "overlay display state should be stored");
    ok &= expect(snapshot.plot_metric == "tof", "plot metric should be stored");
    ok &= expect(snapshot.state_sequence_delta == 3, "state sequence delta should be stored");
    ok &= expect(snapshot.state_receiver_running, "state receiver running flag should be stored");
    ok &= expect(snapshot.gui_vision_tick_delay_ms == 120, "GUI vision tick delay should be stored");
    ok &= expect(snapshot.frame_scale_ms == 4, "frame scale duration should be stored");
    ok &= expect(snapshot.ui_frames_dropped == 24, "UI dropped frame count should be stored");
    ok &= expect(snapshot.plot_samples_displayed == 120, "plot sample count should be stored");
    ok &= expect(snapshot.recovery_attempted, "recovery attempted flag should be stored");
    ok &= expect(snapshot.recovery_hard, "hard recovery flag should be stored");
    ok &= expect(snapshot.connection_state == "CONNECTED", "connection state should be stored");
    ok &= expect(snapshot.attempt == 4, "attempt should be stored");
    ok &= expect(snapshot.last_outage_failures == 2, "last outage failure count should be stored");

    // Case 3: CSV header and row stay aligned even when text fields need escaping.
    tello::MetricsCollector::ExperimentMetadata metadata{};
    metadata.test_id = "E5-001";
    metadata.scenario = "baseline";
    metadata.notes = "note, with comma and \"quote\"";
    metadata.run_mode = "unit";
    metrics.setExperimentMetadata(metadata);
    metrics.setElapsedMs(1234);

    const std::string header = metrics.toCsvHeader();
    const std::string row = metrics.toCsvLine();
    ok &= expect(countCsvColumns(header) == countCsvColumns(row), "CSV header and row column counts should match");
    ok &= expect(row.find("\"note, with comma and \"\"quote\"\"\"") != std::string::npos,
                 "CSV row should escape quoted text fields");

    // Case 4: aggregate link quality exposes control-friendly health state.
    tello::MetricsCollector link_metrics;
    tello::StateReceiver::TelemetryStats link_telemetry{};
    link_telemetry.packets_total = 5;
    link_telemetry.packets_valid = 5;
    link_telemetry.last_packet_age_ms = 30;
    link_telemetry.last_interarrival_ms = 33;
    link_metrics.updateTelemetryStats(link_telemetry);

    auto link_snapshot = link_metrics.getSnapshot();
    ok &= expect(link_snapshot.link_quality.overall == "OK", "fresh telemetry should make overall link OK");
    ok &= expect(link_snapshot.link_quality.safe_for_nonzero_rc,
                 "fresh telemetry should allow nonzero RC when no other channel is degraded");

    link_metrics.updateRuntimeContext(true, 0, 0, 20, 0, false, false, false);
    link_metrics.updateRcLinkStats(-1, 50, 0, -1, false);
    link_snapshot = link_metrics.getSnapshot();
    ok &= expect(link_snapshot.link_quality.rc == "NO_DATA",
                 "active RC without a first sample should report RC as NO_DATA");
    ok &= expect(link_snapshot.link_quality.overall == "OK",
                 "active RC without a first sample should not hide fresh telemetry");
    ok &= expect(!link_snapshot.link_quality.safe_for_nonzero_rc,
                 "active RC without a first sample should not be safe for nonzero RC yet");

    link_metrics.updateRcLinkStats(80, 50, 0, 10, false);
    link_snapshot = link_metrics.getSnapshot();
    ok &= expect(link_snapshot.link_quality.rc == "OK", "regular RC cadence should be OK");
    ok &= expect(link_snapshot.link_quality.safe_for_nonzero_rc,
                 "regular RC cadence should remain safe for nonzero RC");

    link_metrics.setElapsedMs(1000);
    link_metrics.recordCommandResult("land", 20.0, tello::ResponseCode::OK, "ok");
    link_metrics.updateCommandDiagnostics(
        "unit",
        "",
        "attempt=1|send=OK|raw=ok|parsed=OK",
        "executor_call=1|command=land|result=OK|attempt_log=[attempt=1|send=OK|raw=ok|parsed=OK]");
    link_metrics.updateKeepaliveStats(5, 5, 0, 0, 0, "battery?", "79", tello::ResponseCode::OK, 20, 120000, 120000);
    link_snapshot = link_metrics.getSnapshot();
    ok &= expect(link_snapshot.link_quality.command == "OK",
                 "stopped keepalive age should not stale command quality after a successful command");
    ok &= expect(link_snapshot.link_quality.safe_for_nonzero_rc,
                 "stopped keepalive age should not block nonzero RC by itself");

    link_metrics.setElapsedMs(2000);
    link_metrics.recordCommandResult("takeoff", 700.0, tello::ResponseCode::TIMEOUT, "");
    link_snapshot = link_metrics.getSnapshot();
    ok &= expect(link_snapshot.link_quality.command == "DEGRADED",
                 "a command timeout with fresh transport should degrade but not stale link quality");
    ok &= expect(link_snapshot.link_quality.overall == "DEGRADED",
                 "a command timeout with fresh transport should make overall link degraded");
    link_metrics.recordCommandResult(
        "takeoff",
        700.0,
        tello::ResponseCode::TIMEOUT,
        "telemetry_confirmed(takeoff before_h=0 after_h=60)");
    link_snapshot = link_metrics.getSnapshot();
    ok &= expect(link_snapshot.link_quality.command == "OK",
                 "telemetry-confirmed critical command should not degrade live link quality");
    ok &= expect(link_snapshot.link_quality.overall == "OK",
                 "telemetry-confirmed critical command should keep overall link quality OK");
    link_metrics.setElapsedMs(8001);
    link_snapshot = link_metrics.getSnapshot();
    ok &= expect(link_snapshot.link_quality.command == "OK",
                 "an old command timeout should expire from live link quality");
    ok &= expect(link_snapshot.link_quality.overall == "OK",
                 "fresh transport should return overall link quality to OK after timeout window expires");

    link_metrics.recordCommandResult("land", 20.0, tello::ResponseCode::OK, "ok");
    link_metrics.updateRcLinkStats(2500, 50, 1, 10, false);
    link_snapshot = link_metrics.getSnapshot();
    ok &= expect(link_snapshot.link_quality.overall == "BLACKOUT", "very large active RC gap should mark link blackout");
    ok &= expect(!link_snapshot.link_quality.safe_for_nonzero_rc,
                 "active RC blackout should not be safe for nonzero RC");

    // Case 5: reset returns the collector to an empty snapshot.
    metrics.reset();
    snapshot = metrics.getSnapshot();
    ok &= expect(snapshot.command_samples == 0, "reset should clear command samples");
    ok &= expect(snapshot.telemetry.packets_total == 0, "reset should clear telemetry stats");
    ok &= expect(snapshot.telemetry_quality == "NO_DATA", "reset should clear telemetry quality");
    ok &= expect(snapshot.video_rx.packets_total == 0, "reset should clear video stats");
    ok &= expect(snapshot.video_quality == "NO_DATA", "reset should clear video quality");
    ok &= expect(snapshot.link_quality.overall == "NO_DATA", "reset should clear aggregate link quality");
    ok &= expect(snapshot.keyframes == 0, "reset should clear keyframe count");

    if (!ok) {
        return 1;
    }

    std::cout << "[test_metrics_collector] PASS" << std::endl;
    return 0;
}
