#ifndef TELLO_METRICS_HPP
#define TELLO_METRICS_HPP

#include <cstdint>
#include <mutex>
#include <string>

#include "types.hpp"
#include "state_receiver.hpp"

namespace tello {

// ============================================================================
// Metrics Collector
// ============================================================================
/// Central aggregator for runtime metrics produced by command, telemetry,
/// video transport, decode, and experiment workflows.
class MetricsCollector {
public:
    /// Metadata written as a common prefix for experiment CSV rows.
    struct ExperimentMetadata {
        std::string test_id;
        std::string scenario;
        std::string notes;
        std::string run_mode;
    };

    /// Pipeline-neutral video transport counters used by the active FFmpeg Stream path.
    struct VideoTransportStats {
        uint64_t packets_total = 0;
        uint64_t bytes_total = 0;
        uint64_t recv_timeouts = 0;
        uint64_t recv_errors = 0;
        double rx_pps_instant = 0.0;
        double rx_pps_ema = 0.0;
        int64_t last_packet_age_ms = -1;
        int64_t last_interarrival_ms = -1;
        int64_t max_interarrival_ms = 0;
        int64_t last_gap_ms = 0;
        uint64_t last_gap_sequence = 0;
        uint64_t gap_events_300ms = 0;
        uint64_t gap_events_500ms = 0;
        uint64_t gap_events_1000ms = 0;
    };

    /// Pipeline-neutral decode counters produced by FFmpeg Stream.
    struct VideoDecodeStats {
        uint64_t frames_decoded = 0;
        uint64_t decode_errors = 0;
        double decode_fps_ema = 0.0;
        int32_t frame_width = 0;
        int32_t frame_height = 0;
        uint64_t keyframes = 0;
    };

    /// Aggregated link health intended for GUI display and future control gating.
    struct LinkQualitySnapshot {
        std::string overall = "NO_DATA";
        int32_t score = 0;
        bool safe_for_nonzero_rc = false;
        std::string reason = "no fresh transport data";
        std::string telemetry = "NO_DATA";
        std::string video = "NO_DATA";
        std::string command = "NO_DATA";
        std::string rc = "NO_DATA";
        int64_t rc_packet_gap_ms = -1;
        int64_t rc_expected_period_ms = 50;
        uint64_t rc_blackout_count = 0;
        int64_t rc_last_nonzero_age_ms = -1;
        bool rc_safety_override_active = false;
    };

    /// Full-system snapshot of the latest known metrics values.
    struct Snapshot {
        ExperimentMetadata metadata;

        int64_t elapsed_ms = 0;
        uint64_t attempt = 0;

        uint64_t command_samples = 0;
        uint64_t command_successes = 0;
        uint64_t command_failures = 0;
        double command_latency_ms_avg = 0.0;
        double command_latency_ms_min = 0.0;
        double command_latency_ms_max = 0.0;
        double last_command_latency_ms = 0.0;
        int64_t last_command_elapsed_ms = -1;
        int64_t last_command_age_ms = -1;
        uint64_t command_attempt_count = 0;
        uint64_t command_retry_count = 0;
        uint64_t command_timeout_count = 0;
        int64_t command_client_total_ms = 0;
        int64_t command_ensure_sdk_ms = 0;
        int64_t command_executor_total_ms = 0;
        int64_t command_send_ms = 0;
        int64_t command_recv_wait_ms = 0;
        int64_t command_parse_ms = 0;
        int64_t command_executor_recv_wait_total_ms = 0;
        int64_t command_executor_internal_total_ms = 0;
        int64_t command_recovery_ms = 0;
        int64_t command_executor_calls = 0;
        int64_t command_recovery_count = 0;
        std::string last_command;
        std::string last_response;
        ResponseCode last_command_result = ResponseCode::ERROR;
        std::string command_source;
        std::string command_executor_last_error;
        std::string command_executor_attempt_log;
        std::string command_internal_attempt_log;

        StateReceiver::TelemetryStats telemetry;
        std::string telemetry_quality = "NO_DATA";
        int32_t telemetry_quality_score = 0;
        bool telemetry_state_available = false;
        int32_t telemetry_h = 0;
        int32_t telemetry_tof = 0;
        double telemetry_baro = 0.0;
        int32_t telemetry_vgz = 0;
        int32_t telemetry_bat = 0;
        int32_t telemetry_templ = 0;
        int32_t telemetry_temph = 0;
        VideoTransportStats video_rx;
        std::string video_quality = "NO_DATA";
        int32_t video_quality_score = 0;
        uint64_t video_packet_delta = 0;
        LinkQualitySnapshot link_quality;
        VideoDecodeStats decoder;

        int32_t frame_width = 0;
        int32_t frame_height = 0;
        uint64_t keyframes = 0;
        bool paused = false;
        bool overlay_enabled = false;

        bool recovery_attempted = false;
        ResponseCode recovery_result = ResponseCode::OK;
        bool recovery_hard = false;
        std::string recovery_stage;
        bool recovery_command_channel_available = false;
        std::string event;
        std::string log_timestamp;
        std::string log_message;
        std::string connection_state;
        int32_t last_outage_failures = 0;
        std::string plot_metric;
        uint64_t state_sequence_delta = 0;
        bool state_receiver_running = false;

        bool rc_stream_active = false;
        int32_t rc_left_right = 0;
        int32_t rc_forward_back = 0;
        int32_t rc_up_down = 0;
        int32_t rc_yaw = 0;
        bool keepalive_running = false;
        uint64_t keepalive_tick_total = 0;
        uint64_t keepalive_success_total = 0;
        uint64_t keepalive_failure_total = 0;
        uint64_t keepalive_skipped_busy_total = 0;
        uint64_t keepalive_skipped_uninitialized_total = 0;
        std::string keepalive_last_command;
        std::string keepalive_last_response;
        ResponseCode keepalive_last_result = ResponseCode::ERROR;
        int64_t keepalive_last_latency_ms = 0;
        int64_t keepalive_last_success_age_ms = -1;
        int64_t keepalive_last_tick_age_ms = -1;
        bool auto_refresh_active = false;
        bool critical_command_active = false;
        int64_t pause_keepalive_ms = 0;
        int64_t neutral_rc_ms = 0;
        int64_t critical_command_ms = 0;

        int64_t gui_vision_tick_delay_ms = 0;
        int64_t gui_state_tick_delay_ms = 0;
        int64_t vision_refresh_duration_ms = 0;
        int64_t state_refresh_duration_ms = 0;
        int64_t frame_convert_ms = 0;
        int64_t frame_scale_ms = 0;
        int64_t plot_paint_ms = 0;
        int64_t vision_frame_mutex_wait_ms = 0;
        int64_t state_history_fetch_ms = 0;
        int64_t command_mutex_wait_ms = 0;
        uint64_t ui_frames_converted = 0;
        uint64_t ui_frames_dropped = 0;
        uint64_t ui_frames_displayed = 0;
        uint64_t plot_samples_displayed = 0;
    };

    /// Constructor
    MetricsCollector();

    /// Destructor
    ~MetricsCollector() = default;

    /// Reset all metrics counters and computed values.
    void reset();

    /// Store metadata that identifies the current experiment run.
    void setExperimentMetadata(const ExperimentMetadata& metadata);

    /// Store elapsed runtime for the current metrics row.
    void setElapsedMs(int64_t elapsed_ms);

    /// Store current loop attempt/sample index for the current metrics row.
    void setAttempt(uint64_t attempt);

    /// Add one command result and update command latency aggregates.
    void recordCommandResult(
        const std::string& command,
        double latency_ms,
        ResponseCode result,
        const std::string& response
    );

    /// Store the source and executor diagnostic text for the latest command.
    void updateCommandDiagnostics(
        const std::string& source,
        const std::string& executor_last_error,
        const std::string& executor_attempt_log,
        const std::string& internal_attempt_log = ""
    );

    /// Store timing breakdown for the latest command path.
    void updateCommandTimingDiagnostics(
        int64_t client_total_ms,
        int64_t ensure_sdk_ms,
        int64_t executor_total_ms,
        int64_t send_ms,
        int64_t recv_wait_ms,
        int64_t parse_ms,
        int64_t executor_recv_wait_total_ms,
        int64_t executor_internal_total_ms,
        int64_t recovery_ms,
        int64_t executor_calls,
        int64_t recovery_count
    );

    /// Store latest telemetry receiver statistics.
    void updateTelemetryStats(const StateReceiver::TelemetryStats& stats);

    /// Store latest parsed telemetry values that matter for flight diagnosis.
    void updateTelemetryState(const TelloState& state, bool available);

    /// Store latest video stream transport statistics.
    void updateVideoTransportStats(const VideoTransportStats& stats);

    /// Store latest video packet delta for periodic watch rows.
    void setVideoPacketDelta(uint64_t delta);

    /// Store latest decoder statistics.
    void updateDecoderStats(const VideoDecodeStats& stats);

    /// Store latest decoded/displayed frame information.
    void updateFrameInfo(int32_t width, int32_t height, bool keyframe);

    /// Store latest display state for viewer/control-panel rows.
    void updateDisplayState(bool paused, bool overlay_enabled);

    /// Store latest video recovery attempt status.
    void recordRecoveryEvent(
        bool attempted,
        ResponseCode result,
        bool used_hard_recovery,
        const std::string& stage = "",
        bool command_channel_available = false
    );

    /// Store latest high-level event label for CSV export.
    void setEvent(const std::string& event);

    /// Store latest GUI log message and wall-clock timestamp.
    void updateLogMessage(const std::string& timestamp, const std::string& message);

    /// Store latest connection state label for CSV export.
    void setConnectionState(const std::string& state);

    /// Store latest command-session outage failure count.
    void setLastOutageFailures(int32_t failures);

    /// Store UI/state diagnostic context that helps correlate plot changes and telemetry gaps.
    void updatePanelDiagnostics(
        const std::string& plot_metric,
        uint64_t state_sequence_delta,
        bool state_receiver_running
    );

    /// Store latest GUI/API runtime context around a metrics row.
    void updateRuntimeContext(
        bool rc_stream_active,
        int32_t rc_left_right,
        int32_t rc_forward_back,
        int32_t rc_up_down,
        int32_t rc_yaw,
        bool keepalive_running,
        bool auto_refresh_active,
        bool critical_command_active
    );

    /// Store latest RC cadence diagnostics for aggregated link-safety decisions.
    void updateRcLinkStats(
        int64_t rc_packet_gap_ms,
        int64_t rc_expected_period_ms,
        uint64_t rc_blackout_count,
        int64_t rc_last_nonzero_age_ms,
        bool rc_safety_override_active
    );

    /// Store background SDK keepalive diagnostics.
    void updateKeepaliveStats(
        uint64_t tick_total,
        uint64_t success_total,
        uint64_t failure_total,
        uint64_t skipped_busy_total,
        uint64_t skipped_uninitialized_total,
        const std::string& last_command,
        const std::string& last_response,
        ResponseCode last_result,
        int64_t last_latency_ms,
        int64_t last_success_age_ms,
        int64_t last_tick_age_ms
    );

    /// Store timing breakdown for critical-command preflight and execution.
    void updateCriticalCommandDurations(
        int64_t pause_keepalive_ms,
        int64_t neutral_rc_ms,
        int64_t critical_command_ms
    );

    /// Store latest GUI/rendering performance diagnostics.
    void updateGuiPerformance(
        int64_t vision_tick_delay_ms,
        int64_t state_tick_delay_ms,
        int64_t vision_refresh_duration_ms,
        int64_t state_refresh_duration_ms,
        int64_t frame_convert_ms,
        int64_t frame_scale_ms,
        int64_t plot_paint_ms,
        int64_t vision_frame_mutex_wait_ms,
        int64_t state_history_fetch_ms,
        int64_t command_mutex_wait_ms,
        uint64_t ui_frames_converted,
        uint64_t ui_frames_dropped,
        uint64_t ui_frames_displayed,
        uint64_t plot_samples_displayed
    );

    /// Get a thread-safe copy of current metrics.
    Snapshot getSnapshot() const;

    /// Get only the current aggregated link-quality state.
    LinkQualitySnapshot getLinkQualitySnapshot() const;

    /// Export a CSV header matching toCsvLine().
    std::string toCsvHeader() const;

    /// Export current metrics snapshot as one CSV row.
    std::string toCsvLine() const;

private:
    void recomputeLinkQualityLocked();

    mutable std::mutex mutex_;
    Snapshot snapshot_;
};

} // namespace tello

#endif // TELLO_METRICS_HPP
