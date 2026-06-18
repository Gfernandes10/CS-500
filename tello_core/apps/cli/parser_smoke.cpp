#include "tello/state_parser.hpp"
#include <iostream>

int main() {
    tello::StateParser parser;
    tello::TelloState state{};

    // Valid sample similar to Tello state frames.
    const std::string valid =
        "pitch:0;roll:0;yaw:0;vgx:0;vgy:0;vgz:0;"
        "templ:64;temph:67;tof:10;h:0;bat:71;baro:12.34;"
        "time:100;agx:0.01;agy:-0.02;agz:0.98;";

    // Invalid sample (missing separators and malformed token)
    const std::string invalid = "pitch=0;roll:0;bat;";

    const tello::ResponseCode ok_rc = parser.parse(valid, state);
    std::cout << "[valid] rc=" << static_cast<int>(ok_rc)
              << " bat=" << state.bat
              << " yaw=" << state.yaw
              << " baro=" << state.baro
              << std::endl;

    const tello::ResponseCode bad_rc = parser.parse(invalid, state);
    std::cout << "[invalid] rc=" << static_cast<int>(bad_rc)
              << " err=\"" << parser.getLastError() << "\""
              << std::endl;

    return 0;
}