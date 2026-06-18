#include "tello/state_parser.hpp"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "[test_state_parser] FAIL: " << message << std::endl;
        return false;
    }
    return true;
}

} // namespace

int main() {
    tello::StateParser parser;
    bool ok = true;

    // Case 1: valid telemetry frame
    {
        tello::TelloState s{};
        const std::string frame =
            "pitch:1;roll:-2;yaw:3;vgx:4;vgy:5;vgz:6;"
            "templ:64;temph:67;tof:10;h:11;bat:88;baro:12.5;"
            "time:100;agx:0.1;agy:-0.2;agz:0.3;";

        const tello::ResponseCode rc = parser.parse(frame, s);
        ok &= expect(rc == tello::ResponseCode::OK, "valid frame should parse");
        ok &= expect(s.pitch == 1, "pitch should be parsed");
        ok &= expect(s.roll == -2, "roll should be parsed");
        ok &= expect(s.bat == 88, "battery should be parsed");
        ok &= expect(s.baro == 12.5, "baro should be parsed");
    }

    // Case 2: malformed token -> PARSE_ERROR
    {
        tello::TelloState s{};
        const std::string frame = "pitch=0;roll:0;bat;";

        const tello::ResponseCode rc = parser.parse(frame, s);
        ok &= expect(rc == tello::ResponseCode::PARSE_ERROR, "malformed frame should fail");
        ok &= expect(!parser.getLastError().empty(), "error message should be populated");
    }

    // Case 3: missing required keys -> PARSE_ERROR
    {
        tello::TelloState s{};
        const std::string frame = "pitch:0;roll:0;yaw:0;"; // missing bat

        const tello::ResponseCode rc = parser.parse(frame, s);
        ok &= expect(rc == tello::ResponseCode::PARSE_ERROR, "missing required keys should fail");
    }

    // Case 4: partial invalid field value should not crash parsing
    {
        tello::TelloState s{};
        s.vgx = 123; // sentinel value to verify resilience on conversion failure

        const std::string frame =
            "pitch:0;roll:0;yaw:0;bat:50;vgx:not_a_number;";

        const tello::ResponseCode rc = parser.parse(frame, s);
        ok &= expect(rc == tello::ResponseCode::OK, "frame with partial invalid field should still parse");
        ok &= expect(s.vgx == 123, "invalid vgx conversion should preserve previous value");
        ok &= expect(s.bat == 50, "valid fields should still be applied");
    }

    if (!ok) {
        return 1;
    }

    std::cout << "[test_state_parser] PASS" << std::endl;
    return 0;
}
