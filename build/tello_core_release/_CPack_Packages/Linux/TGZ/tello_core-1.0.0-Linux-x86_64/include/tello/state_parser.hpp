#ifndef TELLO_STATE_PARSER_HPP
#define TELLO_STATE_PARSER_HPP

#include "types.hpp"
#include <string>
#include <unordered_map>

namespace tello {

// ============================================================================
// State Parser
// ============================================================================
/// Parses raw Tello telemetry state strings into structured TelloState data.
class StateParser {
public:
    /// Constructor
    StateParser() = default;

    /// Destructor
    ~StateParser() = default;

    /// Parse a full telemetry state string.
    /// Example input:
    /// pitch:0;roll:0;yaw:0;vgx:0;vgy:0;vgz:0;templ:64;temph:67;...
    /// @param raw_state Raw telemetry string received from UDP port 8890.
    /// @param out_state Parsed output state struct.
    /// @return ResponseCode::OK on success, PARSE_ERROR otherwise.
    ResponseCode parse(const std::string& raw_state, TelloState& out_state);

    /// Validate if a raw telemetry string looks structurally correct.
    /// @param raw_state Raw telemetry state.
    /// @return True if format appears valid, false otherwise.
    bool isValidFormat(const std::string& raw_state) const;

    /// Get last parser error message.
    /// @return Human-readable parser error.
    std::string getLastError() const;

private:
    std::string last_error_;

    /// Split k:v; string into key/value map.
    /// @param raw_state Raw telemetry string.
    /// @param fields Output map of parsed fields.
    /// @return ResponseCode::OK on success, PARSE_ERROR otherwise.
    ResponseCode splitFields(
        const std::string& raw_state,
        std::unordered_map<std::string, std::string>& fields
    );

    /// Convert and assign integer field if present.
    /// @param fields Field map.
    /// @param key Field key.
    /// @param target Target integer variable.
    void assignIntField(
        const std::unordered_map<std::string, std::string>& fields,
        const std::string& key,
        int32_t& target
    );

    /// Convert and assign floating-point field if present.
    /// @param fields Field map.
    /// @param key Field key.
    /// @param target Target floating-point variable.
    void assignDoubleField(
        const std::unordered_map<std::string, std::string>& fields,
        const std::string& key,
        double& target
    );
};

} // namespace tello

#endif // TELLO_STATE_PARSER_HPP