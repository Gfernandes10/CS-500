#ifndef TELLO_STATE_RECEIVER_HPP
#define TELLO_STATE_RECEIVER_HPP

#include "types.hpp"
#include "udp_socket.hpp"
#include "state_parser.hpp"
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <thread>
#include <string>

namespace tello {

// ============================================================================
// State Receiver
// ============================================================================
/// Receives telemetry packets on UDP 8890 and keeps latest parsed state.
class StateReceiver {
public:
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

    std::string last_error_;
};

} // namespace tello

#endif // TELLO_STATE_RECEIVER_HPP