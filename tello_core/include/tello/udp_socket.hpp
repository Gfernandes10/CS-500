#ifndef TELLO_UDP_SOCKET_HPP
#define TELLO_UDP_SOCKET_HPP

#include "types.hpp"
#include <string>
#include <cstdint>

namespace tello {

// ============================================================================
// UDP Socket Wrapper
// ============================================================================
/// POSIX UDP socket abstraction for cross-platform networking.
class UdpSocket {
public:
    /// Constructor
    UdpSocket();

    /// Destructor (closes socket if open)
    ~UdpSocket();

    /// Open a UDP socket and connect to a remote host.
    /// @param config Socket configuration (host, port, timeout).
    /// @return ResponseCode::OK on success, error code otherwise.
    ResponseCode open(const SocketConfig& config);

    /// Bind a UDP socket to a local port (for receiving).
    /// @param local_ip Local IP address (e.g., "0.0.0.0" for all interfaces).
    /// @param port Local UDP port to bind to.
    /// @param timeout_ms Receive timeout in milliseconds.
    /// @return ResponseCode::OK on success, error code otherwise.
    ResponseCode bind(const std::string& local_ip, uint16_t port, int32_t timeout_ms);

    /// Send data to the connected remote host.
    /// @param data Pointer to buffer to send.
    /// @param length Number of bytes to send.
    /// @return Number of bytes sent on success, negative on error.
    int32_t send(const uint8_t* data, int32_t length);

    /// Send string data to the connected remote host.
    /// @param message String message to send.
    /// @return Number of bytes sent on success, negative on error.
    int32_t sendString(const std::string& message);

    /// Receive data from the remote host (blocks until timeout or data available).
    /// @param buffer Pointer to output buffer.
    /// @param buffer_size Maximum bytes to read into buffer.
    /// @param bytes_received Output parameter: number of bytes actually received.
    /// @return ResponseCode::OK on success, TIMEOUT if no data, error otherwise.
    ResponseCode recv(uint8_t* buffer, int32_t buffer_size, int32_t& bytes_received);

    /// Receive data as string (null-terminated).
    /// @param timeout_ms Override timeout for this receive.
    /// @return String received on success, empty string on timeout/error.
    std::string recvString(int32_t timeout_ms = -1);

    /// Close the socket.
    void close();

    /// Check if socket is open and valid.
    /// @return True if socket is open, false otherwise.
    bool isOpen() const;

    /// Get the underlying socket file descriptor (advanced use).
    /// @return Socket FD, or -1 if not open.
    int32_t getFileDescriptor() const;

private:
    int32_t socket_fd_;        ///< POSIX socket file descriptor
    int32_t timeout_ms_;       ///< Receive timeout in milliseconds
    std::string remote_host_;  ///< Connected remote IP
    uint16_t remote_port_;     ///< Connected remote port
};

} // namespace tello

#endif // TELLO_UDP_SOCKET_HPP