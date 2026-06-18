#ifndef TELLO_VIDEO_RECEIVER_HPP
#define TELLO_VIDEO_RECEIVER_HPP

#include "types.hpp"
#include "udp_socket.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tello {

// ============================================================================
// Video Receiver
// ============================================================================
/// Receives raw video UDP packets from Tello on port 11111.
class VideoReceiver {
public:
    struct VideoStats {
        uint64_t packets_total = 0;      ///< Total non-empty UDP video packets
        uint64_t bytes_total = 0;        ///< Total payload bytes received
        uint64_t recv_timeouts = 0;      ///< Socket receive timeouts
        uint64_t recv_errors = 0;        ///< Socket receive errors (non-timeout)
        double rx_pps_instant = 0.0;     ///< Instantaneous packet receive rate (pps)
        double rx_pps_ema = 0.0;         ///< Smoothed packet receive rate (pps, EMA)
        int64_t last_packet_age_ms = -1; ///< Milliseconds since last packet (-1 if none)
    };

    /// Raw packet callback type.
    using PacketCallback = std::function<void(const std::vector<uint8_t>&)>;

    /// Constructor
    VideoReceiver();

    /// Destructor
    ~VideoReceiver();

    /// Start receiving video packets in background thread.
    /// @param local_ip Local bind IP (default: 0.0.0.0).
    /// @param local_port Local video UDP port (default: 11111).
    /// @param timeout_ms Receive timeout in milliseconds.
    /// @return ResponseCode::OK on success, error otherwise.
    ResponseCode start(
        const std::string& local_ip = "0.0.0.0",
        uint16_t local_port = 11111,
        int32_t timeout_ms = 1000
    );

    /// Stop background receiver and close socket.
    void stop();

    /// Check if receiver is running.
    bool isRunning() const;

    /// Register callback for each received packet.
    /// @param callback User callback invoked with packet bytes.
    void setPacketCallback(PacketCallback callback);

    /// Get count of received packets since start.
    uint64_t getPacketCount() const;

    /// Get last error message.
    std::string getLastError() const;

    /// Get current video receive statistics.
    VideoStats getVideoStats() const;

    /// Reset video receive statistics counters and rates.
    void resetVideoStats();

private:
    /// Background receive loop.
    void receiveLoop();

    std::shared_ptr<UdpSocket> socket_;
    PacketCallback packet_callback_;

    mutable std::mutex callback_mutex_;
    std::atomic<bool> running_;
    std::atomic<uint64_t> packet_count_;
    std::thread worker_;

    mutable std::mutex stats_mutex_;
    VideoStats stats_;
    std::chrono::steady_clock::time_point last_packet_tp_;
    bool has_last_packet_tp_;

    std::string last_error_;
};

} // namespace tello

#endif // TELLO_VIDEO_RECEIVER_HPP