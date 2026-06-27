#include "tello/metrics.hpp"

#include <sstream>

namespace tello {

namespace {

struct QualityEstimate {
    std::string label;
    int32_t score = 0;
};

std::string responseCodeToString(ResponseCode code) {
    // Response codes are normalized to stable labels for CSV output.
    switch (code) {
    case ResponseCode::OK:
        return "OK";
    case ResponseCode::ERROR:
        return "ERROR";
    case ResponseCode::TIMEOUT:
        return "TIMEOUT";
    case ResponseCode::PARSE_ERROR:
        return "PARSE_ERROR";
    }
    return "UNKNOWN";
}

QualityEstimate estimateTransportQuality(
    uint64_t packets_total,
    int64_t last_packet_age_ms,
    int64_t last_interarrival_ms
) {
    // Quality is based on current freshness so old gaps do not permanently mark a run bad.
    if (packets_total == 0 || last_packet_age_ms < 0) {
        return {"NO_DATA", 0};
    }

    const bool interarrival_ok = last_interarrival_ms < 0 || last_interarrival_ms <= 300;
    const bool interarrival_degraded = last_interarrival_ms < 0 || last_interarrival_ms <= 500;

    if (last_packet_age_ms <= 200 && interarrival_ok) {
        return {"OK", 100};
    }
    if (last_packet_age_ms <= 500 && interarrival_degraded) {
        return {"DEGRADED", 60};
    }
    return {"STALE", 0};
}

std::string csvEscape(const std::string& value) {
    // CSV escaping preserves commas, quotes, and newlines inside text fields.
    bool needs_quotes = false;
    for (char c : value) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }

    if (!needs_quotes) {
        return value;
    }

    std::ostringstream out;
    out << '"';
    for (char c : value) {
        if (c == '"') {
            out << "\"\"";
        } else {
            out << c;
        }
    }
    out << '"';
    return out.str();
}

} // namespace

MetricsCollector::MetricsCollector()
    // Constructor starts with an empty snapshot and default metadata.
    : mutex_(), snapshot_() {}

void MetricsCollector::reset() {
    // Reset returns the collector to an empty snapshot for a new run.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_ = Snapshot{};
}

void MetricsCollector::setExperimentMetadata(const ExperimentMetadata& metadata) {
    // Experiment metadata is a common CSV prefix used to group runs later.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.metadata = metadata;
}

void MetricsCollector::setElapsedMs(int64_t elapsed_ms) {
    // Elapsed time is stored by the caller because each app owns its run clock.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.elapsed_ms = elapsed_ms;
}

void MetricsCollector::setAttempt(uint64_t attempt) {
    // Attempt stores the caller's current loop/sample index for this row.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.attempt = attempt;
}

void MetricsCollector::recordCommandResult(
    const std::string& command,
    double latency_ms,
    ResponseCode result,
    const std::string& response
) {
    // Command results update latest command context and aggregate latency stats.
    std::lock_guard<std::mutex> lock(mutex_);

    snapshot_.last_command = command;
    snapshot_.last_response = response;
    snapshot_.last_command_result = result;
    snapshot_.command_samples += 1;

    if (result == ResponseCode::OK) {
        snapshot_.command_successes += 1;
    } else {
        snapshot_.command_failures += 1;
    }

    if (snapshot_.command_samples == 1) {
        snapshot_.command_latency_ms_avg = latency_ms;
        snapshot_.command_latency_ms_min = latency_ms;
        snapshot_.command_latency_ms_max = latency_ms;
        return;
    }

    const double previous_total =
        snapshot_.command_latency_ms_avg * static_cast<double>(snapshot_.command_samples - 1);
    snapshot_.command_latency_ms_avg =
        (previous_total + latency_ms) / static_cast<double>(snapshot_.command_samples);

    if (latency_ms < snapshot_.command_latency_ms_min) {
        snapshot_.command_latency_ms_min = latency_ms;
    }
    if (latency_ms > snapshot_.command_latency_ms_max) {
        snapshot_.command_latency_ms_max = latency_ms;
    }
}

void MetricsCollector::updateCommandDiagnostics(
    const std::string& source,
    const std::string& executor_last_error,
    const std::string& executor_attempt_log
) {
    // Command diagnostics identify who sent the command and executor detail text.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.command_source = source;
    snapshot_.command_executor_last_error = executor_last_error;
    snapshot_.command_executor_attempt_log = executor_attempt_log;
}

void MetricsCollector::updateTelemetryStats(const StateReceiver::TelemetryStats& stats) {
    // Telemetry stats are copied and reduced into a control-friendly freshness label.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.telemetry = stats;
    const QualityEstimate quality = estimateTransportQuality(
        stats.packets_total,
        stats.last_packet_age_ms,
        stats.last_interarrival_ms
    );
    snapshot_.telemetry_quality = quality.label;
    snapshot_.telemetry_quality_score = quality.score;
}

void MetricsCollector::updateTelemetryState(const TelloState& state, bool available) {
    // Latest state values help diagnose real flight behavior such as landing.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.telemetry_state_available = available;
    if (!available) {
        snapshot_.telemetry_h = 0;
        snapshot_.telemetry_tof = 0;
        snapshot_.telemetry_baro = 0.0;
        snapshot_.telemetry_vgz = 0;
        snapshot_.telemetry_bat = 0;
        return;
    }

    snapshot_.telemetry_h = state.h;
    snapshot_.telemetry_tof = state.tof;
    snapshot_.telemetry_baro = state.baro;
    snapshot_.telemetry_vgz = state.vgz;
    snapshot_.telemetry_bat = state.bat;
}

void MetricsCollector::updateVideoReceiverStats(const VideoReceiver::VideoStats& stats) {
    // Video transport stats are copied and reduced into a stream freshness label.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.video_rx = stats;
    const QualityEstimate quality = estimateTransportQuality(
        stats.packets_total,
        stats.last_packet_age_ms,
        stats.last_interarrival_ms
    );
    snapshot_.video_quality = quality.label;
    snapshot_.video_quality_score = quality.score;
}

void MetricsCollector::setVideoPacketDelta(uint64_t delta) {
    // Packet delta stores the periodic packet increase since the previous watch row.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.video_packet_delta = delta;
}

void MetricsCollector::updateVideoAssemblerStats(const VideoStreamAssembler::Stats& stats) {
    // NAL assembly stats are copied from the H264 boundary parser.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.nal = stats;
}

void MetricsCollector::updateNalClassificationStats(
    uint64_t sps,
    uint64_t pps,
    uint64_t idr,
    uint64_t non_idr,
    uint64_t other,
    uint64_t decode_gated
) {
    // NAL classification counters preserve stream composition diagnostics.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.nal_sps = sps;
    snapshot_.nal_pps = pps;
    snapshot_.nal_idr = idr;
    snapshot_.nal_non_idr = non_idr;
    snapshot_.nal_other = other;
    snapshot_.nal_decode_gated = decode_gated;
}

void MetricsCollector::updateDecoderStats(const VideoDecoderFfmpeg::Stats& stats) {
    // Decode stats are copied from the FFmpeg decoder backend.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.decoder = stats;
}

void MetricsCollector::updateFrameInfo(int32_t width, int32_t height, bool keyframe) {
    // Frame info stores latest dimensions and counts decoded keyframes as events.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.frame_width = width;
    snapshot_.frame_height = height;
    if (keyframe) {
        snapshot_.keyframes += 1;
    }
}

void MetricsCollector::updateDisplayState(bool paused, bool overlay_enabled) {
    // Display state captures UI-level viewer controls for experiment rows.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.paused = paused;
    snapshot_.overlay_enabled = overlay_enabled;
}

void MetricsCollector::recordRecoveryEvent(
    bool attempted,
    ResponseCode result,
    bool used_hard_recovery,
    const std::string& stage,
    bool command_channel_available
) {
    // Recovery status captures the latest stream/session recovery attempt.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.recovery_attempted = attempted;
    snapshot_.recovery_result = result;
    snapshot_.recovery_hard = used_hard_recovery;
    snapshot_.recovery_stage = stage;
    snapshot_.recovery_command_channel_available = command_channel_available;
}

void MetricsCollector::setEvent(const std::string& event) {
    // Event is a free-form label for notable runtime transitions.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.event = event;
}

void MetricsCollector::setConnectionState(const std::string& state) {
    // Connection state stores the caller's current high-level session state.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.connection_state = state;
}

void MetricsCollector::setLastOutageFailures(int32_t failures) {
    // Last outage failures records how many command attempts failed during the latest outage.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.last_outage_failures = failures;
}

void MetricsCollector::updatePanelDiagnostics(
    const std::string& plot_metric,
    uint64_t state_sequence_delta,
    bool state_receiver_running
) {
    // Panel diagnostics correlate user-facing view state with telemetry continuity.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.plot_metric = plot_metric;
    snapshot_.state_sequence_delta = state_sequence_delta;
    snapshot_.state_receiver_running = state_receiver_running;
}

void MetricsCollector::updateRuntimeContext(
    bool rc_stream_active,
    int32_t rc_left_right,
    int32_t rc_forward_back,
    int32_t rc_up_down,
    int32_t rc_yaw,
    bool keepalive_running,
    bool auto_refresh_active,
    bool critical_command_active
) {
    // Runtime context captures UI/API activity that can interfere with flight commands.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.rc_stream_active = rc_stream_active;
    snapshot_.rc_left_right = rc_left_right;
    snapshot_.rc_forward_back = rc_forward_back;
    snapshot_.rc_up_down = rc_up_down;
    snapshot_.rc_yaw = rc_yaw;
    snapshot_.keepalive_running = keepalive_running;
    snapshot_.auto_refresh_active = auto_refresh_active;
    snapshot_.critical_command_active = critical_command_active;
}

void MetricsCollector::updateCriticalCommandDurations(
    int64_t pause_keepalive_ms,
    int64_t neutral_rc_ms,
    int64_t critical_command_ms
) {
    // Critical-command timings expose where preflight/execution latency occurs.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.pause_keepalive_ms = pause_keepalive_ms;
    snapshot_.neutral_rc_ms = neutral_rc_ms;
    snapshot_.critical_command_ms = critical_command_ms;
}

void MetricsCollector::updateGuiPerformance(
    int64_t vision_tick_delay_ms,
    int64_t state_tick_delay_ms,
    int64_t vision_refresh_duration_ms,
    int64_t state_refresh_duration_ms,
    int64_t frame_convert_ms,
    int64_t frame_scale_ms,
    int64_t plot_paint_ms,
    int64_t vision_frame_mutex_wait_ms,
    int64_t state_history_fetch_ms,
    uint64_t ui_frames_converted,
    uint64_t ui_frames_dropped,
    uint64_t ui_frames_displayed,
    uint64_t plot_samples_displayed
) {
    // GUI diagnostics separate render/event-loop load from network/API behavior.
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot_.gui_vision_tick_delay_ms = vision_tick_delay_ms;
    snapshot_.gui_state_tick_delay_ms = state_tick_delay_ms;
    snapshot_.vision_refresh_duration_ms = vision_refresh_duration_ms;
    snapshot_.state_refresh_duration_ms = state_refresh_duration_ms;
    snapshot_.frame_convert_ms = frame_convert_ms;
    snapshot_.frame_scale_ms = frame_scale_ms;
    snapshot_.plot_paint_ms = plot_paint_ms;
    snapshot_.vision_frame_mutex_wait_ms = vision_frame_mutex_wait_ms;
    snapshot_.state_history_fetch_ms = state_history_fetch_ms;
    snapshot_.ui_frames_converted = ui_frames_converted;
    snapshot_.ui_frames_dropped = ui_frames_dropped;
    snapshot_.ui_frames_displayed = ui_frames_displayed;
    snapshot_.plot_samples_displayed = plot_samples_displayed;
}

MetricsCollector::Snapshot MetricsCollector::getSnapshot() const {
    // Snapshot reads return a copy so callers can inspect without holding a lock.
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshot_;
}

std::string MetricsCollector::toCsvHeader() const {
    // Header is intentionally stable so all MetricsCollector CSV rows align.
    return "test_id,scenario,notes,run_mode,elapsed_ms,"
           "attempt,"
           "command_samples,command_successes,command_failures,"
           "command_latency_ms_avg,command_latency_ms_min,command_latency_ms_max,"
           "last_command,last_response,last_command_result,command_source,command_executor_last_error,command_executor_attempt_log,"
           "telemetry_packets_total,telemetry_packets_valid,telemetry_packets_invalid,"
           "telemetry_timeouts,telemetry_errors,telemetry_hz_instant,telemetry_hz_ema,telemetry_age_ms,"
           "telemetry_interarrival_ms,telemetry_max_interarrival_ms,telemetry_last_gap_ms,telemetry_last_gap_sequence,"
           "telemetry_gap_events_300ms,telemetry_gap_events_500ms,telemetry_gap_events_1000ms,"
           "telemetry_quality,telemetry_quality_score,"
           "telemetry_state_available,telemetry_h,telemetry_tof,telemetry_baro,telemetry_vgz,telemetry_bat,"
           "video_packets_total,video_bytes_total,video_timeouts,video_errors,"
           "video_pps_instant,video_pps_ema,video_age_ms,video_packet_delta,"
           "video_interarrival_ms,video_max_interarrival_ms,video_last_gap_ms,video_last_gap_sequence,"
           "video_gap_events_300ms,video_gap_events_500ms,video_gap_events_1000ms,"
           "video_quality,video_quality_score,"
           "nal_packets_in,nal_bytes_in,nal_units,nal_sps,nal_pps,nal_idr,nal_non_idr,nal_other,nal_decode_gated,nal_resyncs,nal_buffered_bytes,"
           "decoder_nals_in,decoder_packets_sent,decoder_frames_decoded,decoder_errors,decoder_fps_ema,"
           "frame_width,frame_height,keyframes,paused,overlay_enabled,"
           "recovery_attempted,recovery_result,recovery_hard,recovery_stage,recovery_command_channel_available,event,connection_state,last_outage_failures,"
           "plot_metric,state_sequence_delta,state_receiver_running,"
           "rc_stream_active,rc_left_right,rc_forward_back,rc_up_down,rc_yaw,"
           "keepalive_running,auto_refresh_active,critical_command_active,"
           "pause_keepalive_ms,neutral_rc_ms,critical_command_ms,"
           "gui_vision_tick_delay_ms,gui_state_tick_delay_ms,"
           "vision_refresh_duration_ms,state_refresh_duration_ms,"
           "frame_convert_ms,frame_scale_ms,plot_paint_ms,"
           "vision_frame_mutex_wait_ms,state_history_fetch_ms,"
           "ui_frames_converted,ui_frames_dropped,ui_frames_displayed,plot_samples_displayed";
}

std::string MetricsCollector::toCsvLine() const {
    // CSV rows are composed from one locked snapshot copy to keep columns coherent.
    const Snapshot s = getSnapshot();

    std::ostringstream out;
    out << csvEscape(s.metadata.test_id) << ','
        << csvEscape(s.metadata.scenario) << ','
        << csvEscape(s.metadata.notes) << ','
        << csvEscape(s.metadata.run_mode) << ','
        << s.elapsed_ms << ','
        << s.attempt << ','
        << s.command_samples << ','
        << s.command_successes << ','
        << s.command_failures << ','
        << s.command_latency_ms_avg << ','
        << s.command_latency_ms_min << ','
        << s.command_latency_ms_max << ','
        << csvEscape(s.last_command) << ','
        << csvEscape(s.last_response) << ','
        << responseCodeToString(s.last_command_result) << ','
        << csvEscape(s.command_source) << ','
        << csvEscape(s.command_executor_last_error) << ','
        << csvEscape(s.command_executor_attempt_log) << ','
        << s.telemetry.packets_total << ','
        << s.telemetry.packets_valid << ','
        << s.telemetry.packets_invalid << ','
        << s.telemetry.recv_timeouts << ','
        << s.telemetry.recv_errors << ','
        << s.telemetry.rx_hz_instant << ','
        << s.telemetry.rx_hz_ema << ','
        << s.telemetry.last_packet_age_ms << ','
        << s.telemetry.last_interarrival_ms << ','
        << s.telemetry.max_interarrival_ms << ','
        << s.telemetry.last_gap_ms << ','
        << s.telemetry.last_gap_sequence << ','
        << s.telemetry.gap_events_300ms << ','
        << s.telemetry.gap_events_500ms << ','
        << s.telemetry.gap_events_1000ms << ','
        << csvEscape(s.telemetry_quality) << ','
        << s.telemetry_quality_score << ','
        << (s.telemetry_state_available ? 1 : 0) << ','
        << s.telemetry_h << ','
        << s.telemetry_tof << ','
        << s.telemetry_baro << ','
        << s.telemetry_vgz << ','
        << s.telemetry_bat << ','
        << s.video_rx.packets_total << ','
        << s.video_rx.bytes_total << ','
        << s.video_rx.recv_timeouts << ','
        << s.video_rx.recv_errors << ','
        << s.video_rx.rx_pps_instant << ','
        << s.video_rx.rx_pps_ema << ','
        << s.video_rx.last_packet_age_ms << ','
        << s.video_packet_delta << ','
        << s.video_rx.last_interarrival_ms << ','
        << s.video_rx.max_interarrival_ms << ','
        << s.video_rx.last_gap_ms << ','
        << s.video_rx.last_gap_sequence << ','
        << s.video_rx.gap_events_300ms << ','
        << s.video_rx.gap_events_500ms << ','
        << s.video_rx.gap_events_1000ms << ','
        << csvEscape(s.video_quality) << ','
        << s.video_quality_score << ','
        << s.nal.packets_in << ','
        << s.nal.bytes_in << ','
        << s.nal.nal_units_out << ','
        << s.nal_sps << ','
        << s.nal_pps << ','
        << s.nal_idr << ','
        << s.nal_non_idr << ','
        << s.nal_other << ','
        << s.nal_decode_gated << ','
        << s.nal.parse_resyncs << ','
        << s.nal.buffered_bytes << ','
        << s.decoder.nals_in << ','
        << s.decoder.packets_sent << ','
        << s.decoder.frames_decoded << ','
        << s.decoder.decode_errors << ','
        << s.decoder.decode_fps_ema << ','
        << s.frame_width << ','
        << s.frame_height << ','
        << s.keyframes << ','
        << (s.paused ? 1 : 0) << ','
        << (s.overlay_enabled ? 1 : 0) << ','
        << (s.recovery_attempted ? 1 : 0) << ','
        << responseCodeToString(s.recovery_result) << ','
        << (s.recovery_hard ? 1 : 0) << ','
        << csvEscape(s.recovery_stage) << ','
        << (s.recovery_command_channel_available ? 1 : 0) << ','
        << csvEscape(s.event) << ','
        << csvEscape(s.connection_state) << ','
        << s.last_outage_failures << ','
        << csvEscape(s.plot_metric) << ','
        << s.state_sequence_delta << ','
        << (s.state_receiver_running ? 1 : 0) << ','
        << (s.rc_stream_active ? 1 : 0) << ','
        << s.rc_left_right << ','
        << s.rc_forward_back << ','
        << s.rc_up_down << ','
        << s.rc_yaw << ','
        << (s.keepalive_running ? 1 : 0) << ','
        << (s.auto_refresh_active ? 1 : 0) << ','
        << (s.critical_command_active ? 1 : 0) << ','
        << s.pause_keepalive_ms << ','
        << s.neutral_rc_ms << ','
        << s.critical_command_ms << ','
        << s.gui_vision_tick_delay_ms << ','
        << s.gui_state_tick_delay_ms << ','
        << s.vision_refresh_duration_ms << ','
        << s.state_refresh_duration_ms << ','
        << s.frame_convert_ms << ','
        << s.frame_scale_ms << ','
        << s.plot_paint_ms << ','
        << s.vision_frame_mutex_wait_ms << ','
        << s.state_history_fetch_ms << ','
        << s.ui_frames_converted << ','
        << s.ui_frames_dropped << ','
        << s.ui_frames_displayed << ','
        << s.plot_samples_displayed;
    return out.str();
}

} // namespace tello
