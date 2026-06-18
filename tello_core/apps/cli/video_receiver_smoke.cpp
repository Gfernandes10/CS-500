#include "tello/video_receiver.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iostream>
#include <thread>

namespace {

std::atomic<bool> g_keep_running{true};

void handleSignal(int signal) {
    (void)signal;
    g_keep_running = false;
}

} // namespace

int main() {
    std::signal(SIGINT, handleSignal);

    tello::VideoReceiver receiver;

    // NOTE: Tello must be in SDK mode and stream must be enabled (streamon).
    const tello::ResponseCode rc = receiver.start("0.0.0.0", 11111, 500);
    if (rc != tello::ResponseCode::OK) {
        std::cerr << "[video_smoke] failed to start video receiver" << std::endl;
        return 1;
    }

    std::cout << "[video_smoke] listening on UDP 11111 (Ctrl+C to stop)" << std::endl;

    uint64_t last_count = 0;
    auto last_tp = std::chrono::steady_clock::now();

    while (g_keep_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        const uint64_t count = receiver.getPacketCount();
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last_tp).count();
        const uint64_t delta = count - last_count;
        const double pps = (dt > 0.0) ? (static_cast<double>(delta) / dt) : 0.0;

        std::cout << "[video_smoke] packets_total=" << count
                  << " delta=" << delta
                  << " pps=" << pps
                  << std::endl;

        const std::string err = receiver.getLastError();
        if (!err.empty()) {
            std::cout << "[video_smoke] last_error=\"" << err << "\"" << std::endl;
        }

        last_count = count;
        last_tp = now;
    }

    receiver.stop();
    std::cout << "[video_smoke] stopped" << std::endl;
    return 0;
}
