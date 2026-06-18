#include "tello/state_parser.hpp"

#include <cctype>
#include <sstream>

namespace tello {
namespace {

std::string trim(const std::string& input) {
    size_t start = 0;
    while (start < input.size() &&
           std::isspace(static_cast<unsigned char>(input[start])) != 0) {
        ++start;
    }

    size_t end = input.size();
    while (end > start &&
           std::isspace(static_cast<unsigned char>(input[end - 1])) != 0) {
        --end;
    }

    return input.substr(start, end - start);
}

bool hasRequiredKeys(const std::unordered_map<std::string, std::string>& fields) {
    // Minimal set to consider the packet as a valid telemetry frame.
    return fields.find("pitch") != fields.end() &&
           fields.find("roll") != fields.end() &&
           fields.find("yaw") != fields.end() &&
           fields.find("bat") != fields.end();
}

} // namespace

ResponseCode StateParser::parse(const std::string& raw_state, TelloState& out_state) {
    last_error_.clear();

    if (!isValidFormat(raw_state)) {
        if (last_error_.empty()) {
            last_error_ = "Invalid telemetry format";
        }
        return ResponseCode::PARSE_ERROR;
    }

    std::unordered_map<std::string, std::string> fields;
    const ResponseCode split_rc = splitFields(raw_state, fields);
    if (split_rc != ResponseCode::OK) {
        return split_rc;
    }

    if (!hasRequiredKeys(fields)) {
        last_error_ = "Missing required telemetry keys";
        return ResponseCode::PARSE_ERROR;
    }

    // Attitude
    assignIntField(fields, "pitch", out_state.pitch);
    assignIntField(fields, "roll", out_state.roll);
    assignIntField(fields, "yaw", out_state.yaw);

    // Velocity
    assignIntField(fields, "vgx", out_state.vgx);
    assignIntField(fields, "vgy", out_state.vgy);
    assignIntField(fields, "vgz", out_state.vgz);

    // Temperature
    assignIntField(fields, "templ", out_state.templ);
    assignIntField(fields, "temph", out_state.temph);

    // Sensors
    assignIntField(fields, "tof", out_state.tof);
    assignIntField(fields, "h", out_state.h);
    assignIntField(fields, "bat", out_state.bat);

    // Barometer/time
    assignDoubleField(fields, "baro", out_state.baro);
    assignIntField(fields, "time", out_state.time);

    // Acceleration
    assignDoubleField(fields, "agx", out_state.agx);
    assignDoubleField(fields, "agy", out_state.agy);
    assignDoubleField(fields, "agz", out_state.agz);

    // Mission pad (optional in many frames)
    assignIntField(fields, "mid", out_state.mid);
    assignIntField(fields, "x", out_state.x);
    assignIntField(fields, "y", out_state.y);
    assignIntField(fields, "z", out_state.z);

    return ResponseCode::OK;
}

bool StateParser::isValidFormat(const std::string& raw_state) const {
    const std::string s = trim(raw_state);
    if (s.empty()) {
        return false;
    }

    // Tello state frames are key:value;key:value;...
    if (s.find(':') == std::string::npos || s.find(';') == std::string::npos) {
        return false;
    }

    return true;
}

std::string StateParser::getLastError() const {
    return last_error_;
}

ResponseCode StateParser::splitFields(
    const std::string& raw_state,
    std::unordered_map<std::string, std::string>& fields
) {
    fields.clear();
    const std::string s = trim(raw_state);
    if (s.empty()) {
        last_error_ = "Telemetry string is empty";
        return ResponseCode::PARSE_ERROR;
    }

    std::stringstream ss(s);
    std::string token;

    while (std::getline(ss, token, ';')) {
        token = trim(token);
        if (token.empty()) {
            // Trailing ';' produces empty token, which is normal.
            continue;
        }

        const size_t sep = token.find(':');
        if (sep == std::string::npos || sep == 0 || sep == token.size() - 1) {
            last_error_ = "Malformed telemetry token: " + token;
            return ResponseCode::PARSE_ERROR;
        }

        const std::string key = trim(token.substr(0, sep));
        const std::string value = trim(token.substr(sep + 1));

        if (key.empty() || value.empty()) {
            last_error_ = "Empty key/value in telemetry token: " + token;
            return ResponseCode::PARSE_ERROR;
        }

        fields[key] = value;
    }

    if (fields.empty()) {
        last_error_ = "No telemetry fields parsed";
        return ResponseCode::PARSE_ERROR;
    }

    return ResponseCode::OK;
}

void StateParser::assignIntField(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string& key,
    int32_t& target
) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return;
    }

    try {
        const int value = std::stoi(it->second);
        target = static_cast<int32_t>(value);
    } catch (...) {
        // Keep old value; parser remains resilient to partial malformed fields.
    }
}

void StateParser::assignDoubleField(
    const std::unordered_map<std::string, std::string>& fields,
    const std::string& key,
    double& target
) {
    const auto it = fields.find(key);
    if (it == fields.end()) {
        return;
    }

    try {
        target = std::stod(it->second);
    } catch (...) {
        // Keep old value; parser remains resilient to partial malformed fields.
    }
}

} // namespace tello