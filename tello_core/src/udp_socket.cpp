#include "tello/udp_socket.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace tello {
namespace {

// Converts milliseconds to timeval, the format required by SO_RCVTIMEO.
timeval toTimeval(int32_t timeout_ms) {
    const int32_t safe_ms = (timeout_ms < 0) ? 0 : timeout_ms;
    timeval tv{};
    tv.tv_sec = static_cast<time_t>(safe_ms / 1000);
    tv.tv_usec = static_cast<suseconds_t>((safe_ms % 1000) * 1000);
    return tv;
}

// recv() timeouts map to EAGAIN/EWOULDBLOCK on Linux.
bool isTimeoutErrno(int err) {
    return err == EAGAIN || err == EWOULDBLOCK;
}

} // namespace

UdpSocket::UdpSocket()
    : socket_fd_(-1), timeout_ms_(1000), remote_host_(""), remote_port_(0) {}

UdpSocket::~UdpSocket() {
    close();
}

ResponseCode UdpSocket::open(const SocketConfig& config) {
    // If this instance was previously opened, start from a clean state.
    close();

    // Create a UDP IPv4 socket (datagram-based, connectionless transport).
    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        return ResponseCode::ERROR;
    }

    timeout_ms_ = config.timeout_ms;
    timeval tv = toTimeval(timeout_ms_);
    // Apply receive timeout so recv() does not block forever.
    if (::setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        close();
        return ResponseCode::ERROR;
    }

    // Optional fixed local bind for command channel stability across runs.
    if (config.local_port != 0) {
        sockaddr_in local_addr{};
        local_addr.sin_family = AF_INET;
        local_addr.sin_port = htons(config.local_port);

        const int local_pton_rc = ::inet_pton(AF_INET, config.local_ip.c_str(), &local_addr.sin_addr);
        if (local_pton_rc != 1) {
            close();
            return ResponseCode::ERROR;
        }

        if (::bind(
                socket_fd_,
                reinterpret_cast<const sockaddr*>(&local_addr),
                static_cast<socklen_t>(sizeof(local_addr))) != 0) {
            close();
            return ResponseCode::ERROR;
        }
    }

    sockaddr_in remote_addr{};
    remote_addr.sin_family = AF_INET;
    // htons converts host byte order to network byte order.
    remote_addr.sin_port = htons(config.port);

    // Convert textual IP (e.g. "192.168.10.1") to binary sockaddr format.
    const int pton_rc = ::inet_pton(AF_INET, config.host.c_str(), &remote_addr.sin_addr);
    if (pton_rc != 1) {
        close();
        return ResponseCode::ERROR;
    }

    // For UDP, connect() fixes the default peer and lets us use send/recv.
    if (::connect(
            socket_fd_,
            reinterpret_cast<const sockaddr*>(&remote_addr),
            static_cast<socklen_t>(sizeof(remote_addr))) != 0) {
        close();
        return ResponseCode::ERROR;
    }

    remote_host_ = config.host;
    remote_port_ = config.port;
    return ResponseCode::OK;
}

ResponseCode UdpSocket::bind(const std::string& local_ip, uint16_t port, int32_t timeout_ms) {
    // Rebind means rebuilding the socket from scratch.
    close();

    socket_fd_ = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        return ResponseCode::ERROR;
    }

    sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);

    const int pton_rc = ::inet_pton(AF_INET, local_ip.c_str(), &local_addr.sin_addr);
    if (pton_rc != 1) {
        close();
        return ResponseCode::ERROR;
    }

    // Binds this socket to a local interface/port to receive packets.
    if (::bind(
            socket_fd_,
            reinterpret_cast<const sockaddr*>(&local_addr),
            static_cast<socklen_t>(sizeof(local_addr))) != 0) {
        close();
        return ResponseCode::ERROR;
    }

    timeout_ms_ = timeout_ms;
    timeval tv = toTimeval(timeout_ms_);
    if (::setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) != 0) {
        close();
        return ResponseCode::ERROR;
    }

    remote_host_.clear();
    remote_port_ = 0;
    return ResponseCode::OK;
}

int32_t UdpSocket::send(const uint8_t* data, int32_t length) {
    // Validate parameters and socket state before calling syscall.
    if (socket_fd_ < 0 || data == nullptr || length <= 0) {
        return -1;
    }

    const ssize_t sent = ::send(
        socket_fd_,
        data,
        static_cast<size_t>(length),
        0);

    if (sent < 0) {
        return -1;
    }

    return static_cast<int32_t>(sent);
}

int32_t UdpSocket::sendString(const std::string& message) {
    // Empty command is considered invalid at socket layer.
    if (message.empty()) {
        return -1;
    }

    const auto* bytes = reinterpret_cast<const uint8_t*>(message.data());
    return send(bytes, static_cast<int32_t>(message.size()));
}

ResponseCode UdpSocket::recv(uint8_t* buffer, int32_t buffer_size, int32_t& bytes_received) {
    // Keep output deterministic on failure paths.
    bytes_received = 0;

    if (socket_fd_ < 0 || buffer == nullptr || buffer_size <= 0) {
        return ResponseCode::ERROR;
    }

    const ssize_t received = ::recv(
        socket_fd_,
        buffer,
        static_cast<size_t>(buffer_size),
        0);

    if (received < 0) {
        const int err = errno;
        // Timeout is not a hard error for command/telemetry workflows.
        if (isTimeoutErrno(err)) {
            return ResponseCode::TIMEOUT;
        }
        return ResponseCode::ERROR;
    }

    bytes_received = static_cast<int32_t>(received);
    return ResponseCode::OK;
}

std::string UdpSocket::recvString(int32_t timeout_ms) {
    if (socket_fd_ < 0) {
        return "";
    }

    // Optional per-call timeout override.
    timeval old_tv{};
    socklen_t old_tv_len = static_cast<socklen_t>(sizeof(old_tv));
    bool restore_old_timeout = false;

    if (timeout_ms >= 0) {
        // Save current timeout so this helper does not mutate socket behavior globally.
        if (::getsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &old_tv, &old_tv_len) == 0) {
            restore_old_timeout = true;
        }

        const timeval new_tv = toTimeval(timeout_ms);
        (void)::setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &new_tv, sizeof(new_tv));
    }

    // Typical command/state payloads are small; 2KB is enough for this stage.
    uint8_t buffer[2048];
    int32_t bytes_received = 0;
    const ResponseCode rc = recv(buffer, static_cast<int32_t>(sizeof(buffer)), bytes_received);

    if (restore_old_timeout) {
        // Restore original timeout to avoid side effects on next calls.
        (void)::setsockopt(socket_fd_, SOL_SOCKET, SO_RCVTIMEO, &old_tv, sizeof(old_tv));
    }

    if (rc != ResponseCode::OK || bytes_received <= 0) {
        return "";
    }

    return std::string(reinterpret_cast<const char*>(buffer), static_cast<size_t>(bytes_received));
}

void UdpSocket::close() {
    if (socket_fd_ >= 0) {
        // close() releases the OS file descriptor; always reset local state after.
        (void)::close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool UdpSocket::isOpen() const {
    return socket_fd_ >= 0;
}

int32_t UdpSocket::getFileDescriptor() const {
    return socket_fd_;
}

} // namespace tello
