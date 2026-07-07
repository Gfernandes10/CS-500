#include "tello/tello_client.hpp"

#ifdef TELLO_HAS_OPENCV
#include <opencv2/core.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_keep_running{true};

void handleSignal(int signal) {
    (void)signal;
    g_keep_running = false;
}

std::string responseCodeToString(tello::ResponseCode rc) {
    switch (rc) {
        case tello::ResponseCode::OK:
            return "OK";
        case tello::ResponseCode::ERROR:
            return "ERROR";
        case tello::ResponseCode::TIMEOUT:
            return "TIMEOUT";
        case tello::ResponseCode::PARSE_ERROR:
            return "PARSE_ERROR";
        default:
            return "UNKNOWN";
    }
}

std::string connectionStateToString(tello::TelloClient::ConnectionState state) {
    switch (state) {
        case tello::TelloClient::ConnectionState::CONNECTED:
            return "CONNECTED";
        case tello::TelloClient::ConnectionState::RECOVERING:
            return "RECOVERING";
        case tello::TelloClient::ConnectionState::DISCONNECTED:
            return "DISCONNECTED";
        default:
            return "UNKNOWN";
    }
}

std::string nowClockString() {
    using Clock = std::chrono::system_clock;
    const auto now = Clock::now();
    const std::time_t now_time = Clock::to_time_t(now);
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&local_tm, "%H:%M:%S");
    return oss.str();
}

#ifdef TELLO_HAS_OPENCV

struct UiButton {
    std::string id;
    std::string label;
    cv::Rect rect;
};

struct AppState {
    tello::TelloClient client;
    bool sdk_ready = false;
    int speed_setpoint = 50;
    std::vector<std::string> log_lines;
    std::vector<UiButton> buttons;
};

AppState* g_state = nullptr;

void appendLog(AppState& state, const std::string& msg) {
    state.log_lines.push_back("[" + nowClockString() + "] " + msg);
    if (state.log_lines.size() > 18) {
        state.log_lines.erase(state.log_lines.begin());
    }
}

bool contains(const UiButton& button, int x, int y) {
    return button.rect.contains(cv::Point(x, y));
}

bool connectSdk(AppState& state) {
    if (!state.client.isInitialized()) {
        const tello::ResponseCode init_rc = state.client.initialize("192.168.10.1", 8889, 8889);
        appendLog(state, "initialize: " + responseCodeToString(init_rc));
        if (init_rc != tello::ResponseCode::OK) {
            state.sdk_ready = false;
            return false;
        }
    }

    const tello::ResponseCode sdk_rc = state.client.enterSdkMode();
    appendLog(state, "command (enterSdkMode): " + responseCodeToString(sdk_rc));
    state.sdk_ready = (sdk_rc == tello::ResponseCode::OK);
    return state.sdk_ready;
}

void runReadCommand(AppState& state, const std::string& cmd) {
    if (!state.sdk_ready) {
        appendLog(state, "connect first (SDK not ready)");
        return;
    }

    std::string response;
    const tello::ResponseCode rc = state.client.sendCommandWithResponse(cmd, response);
    std::string msg = cmd + " => " + responseCodeToString(rc);
    if (!response.empty()) {
        msg += " | response=\"" + response + "\"";
    }
    msg += " | state=" + connectionStateToString(state.client.getConnectionState());
    appendLog(state, msg);
}

void runSetSpeed(AppState& state) {
    if (!state.sdk_ready) {
        appendLog(state, "connect first (SDK not ready)");
        return;
    }

    const std::string cmd = "speed " + std::to_string(state.speed_setpoint);
    const tello::ResponseCode rc = state.client.sendCommand(cmd);
    std::string msg = cmd + " => " + responseCodeToString(rc)
        + " | state=" + connectionStateToString(state.client.getConnectionState());
    appendLog(state, msg);
}

void onMouse(int event, int x, int y, int flags, void* userdata) {
    (void)flags;
    (void)userdata;

    if (event != cv::EVENT_LBUTTONDOWN || g_state == nullptr) {
        return;
    }

    AppState& state = *g_state;
    for (const UiButton& button : state.buttons) {
        if (!contains(button, x, y)) {
            continue;
        }

        if (button.id == "connect") {
            (void)connectSdk(state);
            return;
        }
        if (button.id == "battery") {
            runReadCommand(state, "battery?");
            return;
        }
        if (button.id == "speed_read") {
            runReadCommand(state, "speed?");
            return;
        }
        if (button.id == "time") {
            runReadCommand(state, "time?");
            return;
        }
        if (button.id == "wifi") {
            runReadCommand(state, "wifi?");
            return;
        }
        if (button.id == "sdk") {
            runReadCommand(state, "sdk?");
            return;
        }
        if (button.id == "sn") {
            runReadCommand(state, "sn?");
            return;
        }
        if (button.id == "speed_minus") {
            state.speed_setpoint = std::max(10, state.speed_setpoint - 5);
            appendLog(state, "speed_setpoint=" + std::to_string(state.speed_setpoint));
            return;
        }
        if (button.id == "speed_plus") {
            state.speed_setpoint = std::min(100, state.speed_setpoint + 5);
            appendLog(state, "speed_setpoint=" + std::to_string(state.speed_setpoint));
            return;
        }
        if (button.id == "speed_send") {
            runSetSpeed(state);
            return;
        }
    }
}

void drawButton(cv::Mat& canvas, const UiButton& button, const cv::Scalar& bg, const cv::Scalar& fg) {
    cv::rectangle(canvas, button.rect, bg, cv::FILLED, cv::LINE_AA);
    cv::rectangle(canvas, button.rect, cv::Scalar(35, 35, 35), 1, cv::LINE_AA);
    cv::putText(canvas,
                button.label,
                cv::Point(button.rect.x + 10, button.rect.y + 28),
                cv::FONT_HERSHEY_SIMPLEX,
                0.65,
                fg,
                1,
                cv::LINE_AA);
}

void drawUi(const AppState& state, cv::Mat& canvas) {
    canvas.setTo(cv::Scalar(245, 242, 236));

    cv::putText(canvas,
                "Tello Control Panel (Phase G - MVP A)",
                cv::Point(18, 32),
                cv::FONT_HERSHEY_SIMPLEX,
                0.85,
                cv::Scalar(42, 42, 42),
                2,
                cv::LINE_AA);

    const tello::TelloClient::ConnectionState conn_state = state.client.getConnectionState();
    const std::string status = "State: " + connectionStateToString(conn_state)
        + " | SDK: " + (state.sdk_ready ? "READY" : "NOT_READY")
        + " | speed_setpoint=" + std::to_string(state.speed_setpoint);

    cv::putText(canvas,
                status,
                cv::Point(18, 58),
                cv::FONT_HERSHEY_SIMPLEX,
                0.55,
                cv::Scalar(30, 30, 30),
                1,
                cv::LINE_AA);

    for (const UiButton& button : state.buttons) {
        cv::Scalar bg(218, 229, 238);
        cv::Scalar fg(25, 25, 25);
        if (button.id == "connect") {
            bg = cv::Scalar(180, 232, 197);
        }
        if (button.id == "speed_send") {
            bg = cv::Scalar(245, 214, 170);
        }
        drawButton(canvas, button, bg, fg);
    }

    const cv::Rect log_rect(18, 170, 924, 392);
    cv::rectangle(canvas, log_rect, cv::Scalar(250, 250, 250), cv::FILLED, cv::LINE_AA);
    cv::rectangle(canvas, log_rect, cv::Scalar(90, 90, 90), 1, cv::LINE_AA);
    cv::putText(canvas,
                "Event log",
                cv::Point(log_rect.x + 10, log_rect.y + 24),
                cv::FONT_HERSHEY_SIMPLEX,
                0.6,
                cv::Scalar(40, 40, 40),
                1,
                cv::LINE_AA);

    int y = log_rect.y + 48;
    for (const std::string& line : state.log_lines) {
        cv::putText(canvas,
                    line,
                    cv::Point(log_rect.x + 10, y),
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.48,
                    cv::Scalar(35, 35, 35),
                    1,
                    cv::LINE_AA);
        y += 20;
        if (y > (log_rect.y + log_rect.height - 10)) {
            break;
        }
    }
}

#endif

} // namespace

int main() {
    std::signal(SIGINT, handleSignal);

#ifndef TELLO_HAS_OPENCV
    std::cerr << "[tello_control_panel] OpenCV backend not available. Reconfigure with "
              << "-DTELLO_ENABLE_OPENCV_VIEWER=ON and OpenCV installed." << std::endl;
    return 2;
#else
    AppState state;
    g_state = &state;

    state.buttons = {
        {"connect", "Connect + SDK", cv::Rect(18, 78, 180, 38)},
        {"battery", "battery?", cv::Rect(214, 78, 110, 38)},
        {"speed_read", "speed?", cv::Rect(334, 78, 110, 38)},
        {"time", "time?", cv::Rect(454, 78, 95, 38)},
        {"wifi", "wifi?", cv::Rect(559, 78, 95, 38)},
        {"sdk", "sdk?", cv::Rect(664, 78, 90, 38)},
        {"sn", "sn?", cv::Rect(764, 78, 90, 38)},
        {"speed_minus", "speed -", cv::Rect(18, 122, 100, 34)},
        {"speed_plus", "speed +", cv::Rect(126, 122, 100, 34)},
        {"speed_send", "send speed x", cv::Rect(234, 122, 140, 34)},
    };

    appendLog(state, "Panel ready. Click Connect + SDK first.");
    appendLog(state, "Read commands: battery?, speed?, time?, wifi?, sdk?, sn?");
    appendLog(state, "Set command: speed x (10..100)");

    const std::string window_name = "tello_control_panel";
    cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);
    cv::setMouseCallback(window_name, onMouse, nullptr);

    cv::Mat canvas(580, 960, CV_8UC3);

    while (g_keep_running.load()) {
        // If user closes the window from the title bar, exit the loop cleanly.
        if (cv::getWindowProperty(window_name, cv::WND_PROP_VISIBLE) < 1) {
            break;
        }

        drawUi(state, canvas);
        cv::imshow(window_name, canvas);

        const int key = cv::waitKey(16);
        if (key == 27 || key == 'q' || key == 'Q') {
            break;
        }
    }

    state.client.shutdown();
    cv::destroyWindow(window_name);
    return 0;
#endif
}
