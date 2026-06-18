#ifndef TELLO_METRICS_HPP
#define TELLO_METRICS_HPP

#include <cstdint>
#include <mutex>
#include <string>

namespace tello {

// ============================================================================
// Metrics Collector
// ============================================================================
/// Collects runtime performance metrics (latency, telemetry rate, video FPS).
class MetricsCollector {
public:
    /// Snapshot of current metrics values.
    struct Snapshot {
        double command_latency_ms_avg = 0.0;  ///< Average command latency (ms)
        double command_latency_ms_min = 0.0;  ///< Minimum command latency (ms)
        double command_latency_ms_max = 0.0;  ///< Maximum command latency (ms)

        double telemetry_rate_hz = 0.0;       ///< Telemetry update rate (Hz)
        double video_fps = 0.0;               ///< Video frame rate (FPS)

        uint64_t command_samples = 0;         ///< Number of latency samples
        uint64_t telemetry_packets = 0;       ///< Number of telemetry packets
        uint64_t video_frames = 0;            ///< Number of processed video frames
    };

    /// Constructor
    MetricsCollector();

    /// Destructor
    ~MetricsCollector() = default;

    /// Reset all metrics counters and computed values.
    void reset();

    /// Add one command latency sample.
    /// @param latency_ms Measured latency in milliseconds.
    void addCommandLatencySample(double latency_ms);

    /// Increment telemetry packet counter.
    void incrementTelemetryPacket();

    /// Increment video frame counter.
    void incrementVideoFrame();

    /// Update rates based on elapsed wall time.
    /// @param elapsed_seconds Time elapsed since start/reset.
    void updateRates(double elapsed_seconds);

    /// Get a thread-safe copy of current metrics.
    Snapshot getSnapshot() const;

    /// Export metrics to CSV format line.
    /// @return Comma-separated metrics string.
    std::string toCsvLine() const;

private:
    mutable std::mutex mutex_;
    Snapshot snapshot_;
};

} // namespace tello

#endif // TELLO_METRICS_HPP