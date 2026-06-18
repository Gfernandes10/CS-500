#include "tello/state_receiver.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
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

    tello::StateReceiver receiver;

    const tello::ResponseCode start_rc = receiver.start("0.0.0.0", 8890, 500);
    if (start_rc != tello::ResponseCode::OK) {
        std::cout << "[state_smoke] failed to start receiver" << std::endl;
        return 1;
    }

    std::cout << "[state_smoke] listening on UDP 8890. Press Ctrl+C to stop." << std::endl;

    while (g_keep_running.load()) {
        const tello::StateReceiver::TelemetryStats stats = receiver.getTelemetryStats();

        if (!receiver.hasReceivedState()) {
            std::cout << "[state_smoke] waiting for state packets..." << std::endl;
        } else {
            const tello::TelloState s = receiver.getLatestState();
            std::cout
                << "[state_smoke] "
                << "bat=" << s.bat
                << " h=" << s.h
                << " pitch=" << s.pitch
                << " roll=" << s.roll
                << " yaw=" << s.yaw
                << " vgx=" << s.vgx
                << " vgy=" << s.vgy
                << " vgz=" << s.vgz
                                << " rx_hz=" << stats.rx_hz_ema
                                << " age_ms=" << stats.last_packet_age_ms
                << std::endl;
        }

                std::cout << "[state_smoke] stats"
                                    << " total=" << stats.packets_total
                                    << " valid=" << stats.packets_valid
                                    << " invalid=" << stats.packets_invalid
                                    << " timeouts=" << stats.recv_timeouts
                                    << " errors=" << stats.recv_errors
                                    << std::endl;

        const std::string err = receiver.getLastError();
        if (!err.empty()) {
            std::cout << "[state_smoke] last_error=\"" << err << "\"" << std::endl;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(700));
    }

    receiver.stop();
    std::cout << "[state_smoke] stopped." << std::endl;
    return 0;
}
