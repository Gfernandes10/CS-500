#ifndef TELLO_METRICS_HPP
#define TELLO_METRICS_HPP

#include <cstdint>
#include <mutex>
#include <string>

#include "types.hpp"
#include "state_receiver.hpp"
#include "video_receiver.hpp"
#include "video_stream_assembler.hpp"
#include "video_decoder_ffmpeg.hpp"

namespace tello {

// ============================================================================
// Metrics Collector
// ============================================================================
/// Central aggregator for runtime metrics produced by command, telemetry,
/// video transport, stream assembly, decode, and experiment workflows.
class MetricsCollector {
public:
    /// Metadata written as a common prefix for experiment CSV rows.
    struct ExperimentMetadata {
        std::string test_id;
        std::string scenario;
        std::string notes;
        std::string run_mode;
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
        std::string last_command;
        std::string last_response;
        ResponseCode last_command_result = ResponseCode::ERROR;
        std::string command_source;
        std::string command_executor_last_error;
        std::string command_executor_attempt_log;

        StateReceiver::TelemetryStats telemetry;
        std::string telemetry_quality = "NO_DATA";
        int32_t telemetry_quality_score = 0;
        bool telemetry_state_available = false;
        int32_t telemetry_h = 0;
        int32_t telemetry_tof = 0;
        double telemetry_baro = 0.0;
        int32_t telemetry_vgz = 0;
        int32_t telemetry_bat = 0;
        VideoReceiver::VideoStats video_rx;
        std::string video_quality = "NO_DATA";
        int32_t video_quality_score = 0;
        uint64_t video_packet_delta = 0;
        VideoStreamAssembler::Stats nal;
        uint64_t nal_sps = 0;
        uint64_t nal_pps = 0;
        uint64_t nal_idr = 0;
        uint64_t nal_non_idr = 0;
        uint64_t nal_other = 0;
        uint64_t nal_decode_gated = 0;
        VideoDecoderFfmpeg::Stats decoder;

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
        const std::string& executor_attempt_log
    );

    /// Store latest telemetry receiver statistics.
    void updateTelemetryStats(const StateReceiver::TelemetryStats& stats);

    /// Store latest parsed telemetry values that matter for flight diagnosis.
    void updateTelemetryState(const TelloState& state, bool available);

    /// Store latest video UDP receiver statistics.
    void updateVideoReceiverStats(const VideoReceiver::VideoStats& stats);

    /// Store latest video packet delta for periodic watch rows.
    void setVideoPacketDelta(uint64_t delta);

    /// Store latest H264/NAL assembler statistics.
    void updateVideoAssemblerStats(const VideoStreamAssembler::Stats& stats);

    /// Store latest classified NAL counters.
    void updateNalClassificationStats(
        uint64_t sps,
        uint64_t pps,
        uint64_t idr,
        uint64_t non_idr,
        uint64_t other,
        uint64_t decode_gated
    );

    /// Store latest decoder statistics.
    void updateDecoderStats(const VideoDecoderFfmpeg::Stats& stats);

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
        uint64_t ui_frames_converted,
        uint64_t ui_frames_dropped,
        uint64_t ui_frames_displayed,
        uint64_t plot_samples_displayed
    );

    /// Get a thread-safe copy of current metrics.
    Snapshot getSnapshot() const;

    /// Export a CSV header matching toCsvLine().
    std::string toCsvHeader() const;

    /// Export current metrics snapshot as one CSV row.
    std::string toCsvLine() const;

private:
    mutable std::mutex mutex_;
    Snapshot snapshot_;
};

} // namespace tello

#endif // TELLO_METRICS_HPP
