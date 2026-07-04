#ifndef TELLO_TYPES_HPP
#define TELLO_TYPES_HPP

#include <string>
#include <cstdint>

namespace tello {

// ============================================================================
// Tello State Structure
// ============================================================================
/// Represents the complete state of the Tello drone.
/// Fields populated from UDP state messages (port 8890).
struct TelloState {
    // Attitude (degrees)
    int32_t pitch = 0;  ///< Pitch angle
    int32_t roll = 0;   ///< Roll angle
    int32_t yaw = 0;    ///< Yaw angle
    
    // Velocity (cm/s)
    int32_t vgx = 0;    ///< X-axis velocity
    int32_t vgy = 0;    ///< Y-axis velocity
    int32_t vgz = 0;    ///< Z-axis velocity
    
    // Temperature (Celsius)
    int32_t templ = 0;  ///< Lowest temperature
    int32_t temph = 0;  ///< Highest temperature
    
    // Sensors
    int32_t tof = 0;    ///< Time-of-flight distance (cm)
    int32_t h = 0;      ///< Height (cm)
    int32_t bat = 0;    ///< Battery percentage (0-100)
    
    // Barometer (cm)
    double baro = 0.0;  ///< Barometer measurement
    
    // Time
    int32_t time = 0;   ///< Total flight time (ms)
    
    // Acceleration (m/s²)
    double agx = 0.0;   ///< X-axis acceleration
    double agy = 0.0;   ///< Y-axis acceleration
    double agz = 0.0;   ///< Z-axis acceleration
    
    // Mission Pad Detection (optional)
    int32_t mid = -1;   ///< Mission Pad ID (-1 if none detected)
    int32_t x = 0;      ///< Relative x-coordinate
    int32_t y = 0;      ///< Relative y-coordinate
    int32_t z = 0;      ///< Relative z-coordinate
};

// ============================================================================
// Command Response Codes
// ============================================================================
enum class ResponseCode {
    OK = 0,              ///< Command succeeded
    ERROR = -1,          ///< Command failed
    TIMEOUT = -2,        ///< No response received
    PARSE_ERROR = -3,    ///< Response parsing failed
};

// ============================================================================
// Socket Configuration
// ============================================================================
struct SocketConfig {
    std::string host;           ///< IP address
    uint16_t port = 0;          ///< UDP port
    std::string local_ip = "0.0.0.0";  ///< Local bind IP (optional)
    uint16_t local_port = 0;    ///< Local bind port (0 = ephemeral)
    int32_t timeout_ms = 1000;  ///< Receive timeout (milliseconds)
};

} // namespace tello

#endif // TELLO_TYPES_HPP