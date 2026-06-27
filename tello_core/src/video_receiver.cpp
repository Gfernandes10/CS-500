#include "tello/video_receiver.hpp"

#include <algorithm>
#include <chrono>
#include <thread>
#include <vector>

namespace tello {

VideoReceiver::VideoReceiver()
    : socket_(nullptr),
      packet_callback_(nullptr),
      callback_mutex_(),
      running_(false),
      packet_count_(0),
      worker_(),
    stats_mutex_(),
    stats_(),
    last_packet_tp_(std::chrono::steady_clock::now()),
    has_last_packet_tp_(false),
      last_error_("") {}

VideoReceiver::~VideoReceiver() {
    stop();
}

ResponseCode VideoReceiver::start(const std::string& local_ip, uint16_t local_port, int32_t timeout_ms) {
    if (running_.load()) {
        return ResponseCode::OK;
    }

    socket_ = std::make_shared<UdpSocket>();
    const ResponseCode bind_rc = socket_->bind(local_ip, local_port, timeout_ms);
    if (bind_rc != ResponseCode::OK) {
        last_error_ = "Failed to bind video socket";
        socket_.reset();
        return bind_rc;
    }

    packet_count_ = 0;
    resetVideoStats();
    last_error_.clear();
    running_ = true;

    worker_ = std::thread(&VideoReceiver::receiveLoop, this);
    return ResponseCode::OK;
}

void VideoReceiver::stop() {
    if (!running_.load()) {
        return;
    }

    running_ = false;

    if (socket_ != nullptr) {
        socket_->close();
    }

    if (worker_.joinable()) {
        worker_.join();
    }

    socket_.reset();
}

bool VideoReceiver::isRunning() const {
    return running_.load();
}

void VideoReceiver::setPacketCallback(PacketCallback callback) {
    std::lock_guard<std::mutex> lock(callback_mutex_);
    packet_callback_ = std::move(callback);
}

uint64_t VideoReceiver::getPacketCount() const {
    return packet_count_.load();
}

std::string VideoReceiver::getLastError() const {
    return last_error_;
}

VideoReceiver::VideoStats VideoReceiver::getVideoStats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    VideoStats snapshot = stats_;
    if (has_last_packet_tp_) {
        const auto now = std::chrono::steady_clock::now();
        snapshot.last_packet_age_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_tp_).count();
    } else {
        snapshot.last_packet_age_ms = -1;
    }
    return snapshot;
}

void VideoReceiver::resetVideoStats() {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = VideoStats{};
    last_packet_tp_ = std::chrono::steady_clock::now();
    has_last_packet_tp_ = false;
}

void VideoReceiver::receiveLoop() {
    // Keep buffer large enough for UDP payload bursts from video stream chunks.
    std::vector<uint8_t> buffer(4096);

    while (running_.load()) {
        if (socket_ == nullptr || !socket_->isOpen()) {
            last_error_ = "Video socket is not open";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }

        int32_t bytes_received = 0;
        const ResponseCode recv_rc = socket_->recv(
            buffer.data(),
            static_cast<int32_t>(buffer.size()),
            bytes_received);

        if (!running_.load()) {
            break;
        }

        if (recv_rc == ResponseCode::TIMEOUT) {
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.recv_timeouts;
            }
            continue;
        }

        if (recv_rc != ResponseCode::OK) {
            last_error_ = "Video receive error";
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                ++stats_.recv_errors;
            }
            continue;
        }

        if (bytes_received <= 0) {
            continue;
        }

        ++packet_count_;

        const auto now = std::chrono::steady_clock::now();
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            ++stats_.packets_total;
            stats_.bytes_total += static_cast<uint64_t>(bytes_received);

            if (has_last_packet_tp_) {
                const double dt_s = std::chrono::duration<double>(now - last_packet_tp_).count();
                if (dt_s > 0.0) {
                    const double instant_pps = 1.0 / dt_s;
                    stats_.rx_pps_instant = instant_pps;
                    const double alpha = 0.2;
                    if (stats_.rx_pps_ema <= 0.0) {
                        stats_.rx_pps_ema = instant_pps;
                    } else {
                        stats_.rx_pps_ema = (alpha * instant_pps) + ((1.0 - alpha) * stats_.rx_pps_ema);
                    }
                }

                const int64_t interarrival_ms =
                    std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_tp_).count();
                stats_.last_interarrival_ms = interarrival_ms;
                stats_.max_interarrival_ms = std::max(stats_.max_interarrival_ms, interarrival_ms);
                if (interarrival_ms >= 300) {
                    ++stats_.gap_events_300ms;
                    stats_.last_gap_ms = interarrival_ms;
                    stats_.last_gap_sequence = stats_.packets_total;
                }
                if (interarrival_ms >= 500) {
                    ++stats_.gap_events_500ms;
                }
                if (interarrival_ms >= 1000) {
                    ++stats_.gap_events_1000ms;
                }
            }

            last_packet_tp_ = now;
            has_last_packet_tp_ = true;
        }

        PacketCallback callback_copy;
        {
            std::lock_guard<std::mutex> lock(callback_mutex_);
            callback_copy = packet_callback_;
        }

        if (callback_copy) {
            std::vector<uint8_t> packet(
                buffer.begin(),
                buffer.begin() + static_cast<size_t>(bytes_received));
            callback_copy(packet);
        }
    }
}

} // namespace tello
