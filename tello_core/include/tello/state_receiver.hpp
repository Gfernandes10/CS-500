#ifndef TELLO_STATE_RECEIVER_HPP
#define TELLO_STATE_RECEIVER_HPP

#include "types.hpp"
#include "udp_socket.hpp"
#include "state_parser.hpp"
#include <atomic>
#include <chrono>
#include <deque>
#include <cstddef>
#include <memory>
#include <mutex>
#include <thread>
#include <string>
#include <vector>

namespace tello {

// ============================================================================
// State Receiver
// ============================================================================
/// Receives telemetry packets on UDP 8890 and keeps latest parsed state.
class StateReceiver {
public:
    struct StateSample {
        uint64_t sequence = 0;             ///< Monotonic sample index since receiver start
        int64_t timestamp_ms = 0;          ///< Wall-clock timestamp (Unix epoch ms)
        int64_t recording_elapsed_ms = -1; ///< ms since recording start (-1 if not recording)
        TelloState state{};                ///< Parsed telemetry sample
    };

    struct RcCommandSample {
        uint64_t sequence = 0;             ///< Monotonic RC event index since receiver start
        int64_t timestamp_ms = 0;          ///< Wall-clock timestamp (Unix epoch ms)
        int64_t recording_elapsed_ms = -1; ///< ms since recording start (-1 if not recording)
        int a = 0;                         ///< RC left/right channel [-100, 100]
        int b = 0;                         ///< RC forward/back channel [-100, 100]
        int c = 0;                         ///< RC up/down channel [-100, 100]
        int d = 0;                         ///< RC yaw channel [-100, 100]
        std::string source;                ///< Caller/source tag (manual, stream, etc.)
        ResponseCode response = ResponseCode::ERROR;
    };

    struct TelemetryStats {
        uint64_t packets_total = 0;      ///< Total UDP packets received (non-empty)
        uint64_t packets_valid = 0;      ///< Packets successfully parsed into TelloState
        uint64_t packets_invalid = 0;    ///< Packets received but failed parsing
        uint64_t recv_timeouts = 0;      ///< Socket receive timeouts
        uint64_t recv_errors = 0;        ///< Socket receive errors (non-timeout)
        double rx_hz_instant = 0.0;      ///< Instantaneous receive rate (Hz)
        double rx_hz_ema = 0.0;          ///< Smoothed receive rate (EMA, Hz)
        int64_t last_packet_age_ms = -1; ///< Milliseconds since last packet (-1 if none)
    };

    /// Constructor
    StateReceiver();

    /// Destructor
    ~StateReceiver();

    /// Start background receiver thread.
    /// @param local_ip Local bind IP (default: 0.0.0.0).
    /// @param local_port Local telemetry port (default: 8890).
    /// @param timeout_ms Receive timeout in milliseconds.
    /// @return ResponseCode::OK on success, error otherwise.
    ResponseCode start(
        const std::string& local_ip = "0.0.0.0",
        uint16_t local_port = 8890,
        int32_t timeout_ms = 1000
    );

    /// Stop background receiver thread and close socket.
    void stop();

    /// Check if receiver is currently running.
    bool isRunning() const;

    /// Get a thread-safe copy of latest parsed telemetry state.
    /// @return Latest known state.
    TelloState getLatestState() const;

    /// Check if at least one valid telemetry packet was received.
    bool hasReceivedState() const;

    /// Get last receive/parser error message.
    std::string getLastError() const;

    /// Get current telemetry receive statistics.
    /// @return Thread-safe snapshot of telemetry stats.
    TelemetryStats getTelemetryStats() const;

    /// Reset telemetry statistics counters and rates.
    void resetTelemetryStats();

    /// Configure temporary ring buffer capacity for state samples.
    /// @param capacity Maximum number of most recent samples to retain.
    void setStateBufferCapacity(size_t capacity);

    /// Get current temporary ring buffer capacity.
    size_t getStateBufferCapacity() const;

    /// Get a snapshot of temporary buffered state samples (oldest -> newest).
    std::vector<StateSample> getBufferedStateSamples() const;

    /// Clear temporary buffered state samples.
    void clearBufferedStateSamples();

    /// Start recording state samples continuously (not capped by ring buffer).
    void startStateRecording();

    /// Stop recording state samples.
    void stopStateRecording();

    /// Check whether continuous recording is active.
    bool isStateRecording() const;

    /// Get a snapshot of recorded state samples.
    std::vector<StateSample> getRecordedStateSamples() const;

    /// Get a snapshot of recorded RC command samples.
    std::vector<RcCommandSample> getRecordedRcCommandSamples() const;

    /// Record one RC command event, tied to current recording timeline when enabled.
    void recordRcCommandSample(int a, int b, int c, int d, const std::string& source, ResponseCode response);

    /// Clear recorded state samples.
    void clearRecordedStateSamples();

    /// Export recorded state samples to CSV file.
    /// @param file_path Output CSV path.
    /// @return ResponseCode::OK on success, ResponseCode::ERROR on failure.
    ResponseCode exportRecordedStateCsv(const std::string& file_path) const;

private:
    /// Background receive loop.
    void receiveLoop();

    std::shared_ptr<UdpSocket> socket_;
    StateParser parser_;

    mutable std::mutex state_mutex_;
    TelloState latest_state_;
    std::atomic<bool> running_;
    std::atomic<bool> has_state_;
    std::thread worker_;

    mutable std::mutex stats_mutex_;
    TelemetryStats stats_;
    std::chrono::steady_clock::time_point last_packet_tp_;
    bool has_last_packet_tp_;

    mutable std::mutex history_mutex_;
    size_t state_buffer_capacity_;
    std::deque<StateSample> state_buffer_;
    std::vector<StateSample> recorded_samples_;
    std::vector<RcCommandSample> recorded_rc_samples_;
    bool recording_enabled_;
    int64_t recording_start_timestamp_ms_;
    uint64_t next_state_sequence_;
    uint64_t next_rc_sequence_;

    std::string last_error_;
};

} // namespace tello

#endif // TELLO_STATE_RECEIVER_HPP