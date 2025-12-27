#include "TCPClientInterface.h"
#include "HDLC.h"

#include <Transport.h>
#include <Log.h>

#include <memory>

#ifdef ARDUINO
// ESP32 lwIP socket headers
#include <lwip/sockets.h>
#include <lwip/netdb.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#endif

using namespace RNS;

TCPClientInterface::TCPClientInterface(const char* name /*= "TCPClientInterface"*/)
    : RNS::InterfaceImpl(name) {

    _IN = true;
    _OUT = true;
    _bitrate = BITRATE_GUESS;
    _HW_MTU = HW_MTU;
}

/*virtual*/ TCPClientInterface::~TCPClientInterface() {
    stop();
}

/*virtual*/ bool TCPClientInterface::start() {
    _online = false;

    TRACE("TCPClientInterface: target host: " + _target_host);
    TRACE("TCPClientInterface: target port: " + std::to_string(_target_port));

    if (_target_host.empty()) {
        ERROR("TCPClientInterface: No target host configured");
        return false;
    }

    // WiFi connection is handled externally (in main.cpp)
    // Attempt initial connection
    if (!connect()) {
        INFO("TCPClientInterface: Initial connection failed, will retry in background");
        // Don't return false - we'll reconnect in loop()
    }

    return true;
}

bool TCPClientInterface::connect() {
    TRACE("TCPClientInterface: Connecting to " + _target_host + ":" + std::to_string(_target_port));

#ifdef ARDUINO
    // Set connection timeout
    _client.setTimeout(CONNECT_TIMEOUT_MS);

    if (!_client.connect(_target_host.c_str(), _target_port)) {
        DEBUG("TCPClientInterface: Connection failed");
        return false;
    }

    // Configure socket options
    configure_socket();

    INFO("TCPClientInterface: Connected to " + _target_host + ":" + std::to_string(_target_port));
    _online = true;
    _frame_buffer.clear();
    return true;

#else
    // Resolve target host
    struct in_addr target_addr;
    if (inet_aton(_target_host.c_str(), &target_addr) == 0) {
        struct hostent* host_ent = gethostbyname(_target_host.c_str());
        if (host_ent == nullptr || host_ent->h_addr_list[0] == nullptr) {
            ERROR("TCPClientInterface: Unable to resolve host " + _target_host);
            return false;
        }
        _target_address = *((in_addr_t*)(host_ent->h_addr_list[0]));
    } else {
        _target_address = target_addr.s_addr;
    }

    // Create TCP socket
    _socket = socket(PF_INET, SOCK_STREAM, 0);
    if (_socket < 0) {
        ERROR("TCPClientInterface: Unable to create socket, error " + std::to_string(errno));
        return false;
    }

    // Set non-blocking for connect timeout
    int flags = fcntl(_socket, F_GETFL, 0);
    fcntl(_socket, F_SETFL, flags | O_NONBLOCK);

    // Connect to server
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = _target_address;
    server_addr.sin_port = htons(_target_port);

    int result = ::connect(_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
    if (result < 0 && errno != EINPROGRESS) {
        close(_socket);
        _socket = -1;
        ERROR("TCPClientInterface: Connect failed, error " + std::to_string(errno));
        return false;
    }

    // Wait for connection with timeout
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(_socket, &write_fds);
    struct timeval timeout;
    timeout.tv_sec = CONNECT_TIMEOUT_MS / 1000;
    timeout.tv_usec = (CONNECT_TIMEOUT_MS % 1000) * 1000;

    result = select(_socket + 1, nullptr, &write_fds, nullptr, &timeout);
    if (result <= 0) {
        close(_socket);
        _socket = -1;
        DEBUG("TCPClientInterface: Connection timeout");
        return false;
    }

    // Check if connection succeeded
    int sock_error = 0;
    socklen_t len = sizeof(sock_error);
    getsockopt(_socket, SOL_SOCKET, SO_ERROR, &sock_error, &len);
    if (sock_error != 0) {
        close(_socket);
        _socket = -1;
        DEBUG("TCPClientInterface: Connection failed, error " + std::to_string(sock_error));
        return false;
    }

    // Restore blocking mode for normal operation
    fcntl(_socket, F_SETFL, flags);

    // Configure socket options
    configure_socket();

    INFO("TCPClientInterface: Connected to " + _target_host + ":" + std::to_string(_target_port));
    _online = true;
    _frame_buffer.clear();
    return true;
#endif
}

void TCPClientInterface::configure_socket() {
#ifdef ARDUINO
    // Get underlying socket fd for setsockopt
    int fd = _client.fd();
    if (fd < 0) {
        DEBUG("TCPClientInterface: Could not get socket fd for configuration");
        return;
    }

    // TCP_NODELAY - disable Nagle's algorithm
    int flag = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Enable TCP keepalive
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));

    // Keepalive parameters (may not all be available on ESP32 lwIP)
#ifdef TCP_KEEPIDLE
    int keepidle = TCP_KEEPIDLE_SEC;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
#endif
#ifdef TCP_KEEPINTVL
    int keepintvl = TCP_KEEPINTVL_SEC;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
#endif
#ifdef TCP_KEEPCNT
    int keepcnt = TCP_KEEPCNT_PROBES;
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));
#endif

    TRACE("TCPClientInterface: Socket configured with TCP_NODELAY and keepalive");

#else
    // TCP_NODELAY
    int flag = 1;
    setsockopt(_socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // Enable TCP keepalive
    setsockopt(_socket, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));

    // Keepalive parameters
    int keepidle = TCP_KEEPIDLE_SEC;
    int keepintvl = TCP_KEEPINTVL_SEC;
    int keepcnt = TCP_KEEPCNT_PROBES;
    setsockopt(_socket, IPPROTO_TCP, TCP_KEEPIDLE, &keepidle, sizeof(keepidle));
    setsockopt(_socket, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
    setsockopt(_socket, IPPROTO_TCP, TCP_KEEPCNT, &keepcnt, sizeof(keepcnt));

    // TCP_USER_TIMEOUT (Linux 2.6.37+)
#ifdef TCP_USER_TIMEOUT
    int user_timeout = 24000;  // 24 seconds, matches Python RNS
    setsockopt(_socket, IPPROTO_TCP, TCP_USER_TIMEOUT, &user_timeout, sizeof(user_timeout));
#endif

    TRACE("TCPClientInterface: Socket configured with TCP_NODELAY, keepalive, and timeouts");
#endif
}

void TCPClientInterface::disconnect() {
    DEBUG("TCPClientInterface: Disconnecting");

#ifdef ARDUINO
    _client.stop();
#else
    if (_socket >= 0) {
        close(_socket);
        _socket = -1;
    }
#endif

    _online = false;
    _frame_buffer.clear();
}

void TCPClientInterface::handle_disconnect() {
    if (_online) {
        INFO("TCPClientInterface: Connection lost, will attempt reconnection");
        disconnect();
    }
}

/*virtual*/ void TCPClientInterface::stop() {
    disconnect();
}

/*virtual*/ void TCPClientInterface::loop() {
    static int loop_count = 0;
    loop_count++;
    if (loop_count % 100 == 1) {
#ifdef ARDUINO
        DEBUG("TCPClientInterface::loop() #" + std::to_string(loop_count) + ", online=" + std::to_string(_online) + ", connected=" + std::to_string(_client.connected()));
#else
        DEBUG("TCPClientInterface::loop() #" + std::to_string(loop_count) + ", online=" + std::to_string(_online) + ", socket=" + std::to_string(_socket));
#endif
    }

    // Handle reconnection if not connected
    if (!_online) {
        if (_initiator) {
#ifdef ARDUINO
            uint32_t now = millis();
#else
            uint32_t now = static_cast<uint32_t>(Utilities::OS::time() * 1000);
#endif
            if (now - _last_connect_attempt >= RECONNECT_WAIT_MS) {
                _last_connect_attempt = now;
                DEBUG("TCPClientInterface: Attempting reconnection...");
                connect();
            }
        }
        return;
    }

    // Check connection status
#ifdef ARDUINO
    if (!_client.connected()) {
        handle_disconnect();
        return;
    }

    // Read available data
    while (_client.available() > 0) {
        uint8_t byte = _client.read();
        _frame_buffer.append(byte);
    }
#else
    // Non-blocking read
    uint8_t buf[4096];
    ssize_t len = recv(_socket, buf, sizeof(buf), MSG_DONTWAIT);
    if (len > 0) {
        DEBUG("TCPClientInterface: Received " + std::to_string(len) + " bytes");
        _frame_buffer.append(buf, len);
    } else if (len == 0) {
        // Connection closed by peer
        DEBUG("TCPClientInterface: recv returned 0 - connection closed");
        handle_disconnect();
        return;
    } else {
        int err = errno;
        if (err != EAGAIN && err != EWOULDBLOCK) {
            // Socket error
            ERROR("TCPClientInterface: recv error " + std::to_string(err));
            handle_disconnect();
            return;
        }
        // EAGAIN/EWOULDBLOCK - normal for non-blocking, just no data yet
    }
#endif

    // Process any complete frames
    extract_and_process_frames();
}

void TCPClientInterface::extract_and_process_frames() {
    // Find and process complete HDLC frames: [FLAG][data][FLAG]

    while (true) {
        // Find first FLAG byte
        int start = -1;
        for (size_t i = 0; i < _frame_buffer.size(); ++i) {
            if (_frame_buffer.data()[i] == HDLC::FLAG) {
                start = static_cast<int>(i);
                break;
            }
        }

        if (start < 0) {
            // No FLAG found, discard buffer (garbage data before any frame)
            _frame_buffer.clear();
            break;
        }

        // Discard data before first FLAG
        if (start > 0) {
            _frame_buffer = _frame_buffer.mid(start);
        }

        // Find end FLAG (skip the start FLAG at position 0)
        int end = -1;
        for (size_t i = 1; i < _frame_buffer.size(); ++i) {
            if (_frame_buffer.data()[i] == HDLC::FLAG) {
                end = static_cast<int>(i);
                break;
            }
        }

        if (end < 0) {
            // Incomplete frame, wait for more data
            break;
        }

        // Extract frame content between FLAGS (excluding the FLAGS)
        Bytes frame_content = _frame_buffer.mid(1, end - 1);

        // Remove processed frame from buffer (keep data after end FLAG)
        _frame_buffer = _frame_buffer.mid(end);

        // Skip empty frames (consecutive FLAGs)
        if (frame_content.size() == 0) {
            continue;
        }

        // Unescape frame
        Bytes unescaped = HDLC::unescape(frame_content);
        if (unescaped.size() == 0) {
            DEBUG("TCPClientInterface: HDLC unescape error, discarding frame");
            continue;
        }

        // Validate minimum frame size (matches Python RNS HEADER_MINSIZE check)
        if (unescaped.size() < Type::Reticulum::HEADER_MINSIZE) {
            TRACE("TCPClientInterface: Frame too small (" + std::to_string(unescaped.size()) + " bytes), discarding");
            continue;
        }

        // Pass to transport layer
        DEBUG(toString() + ": Received frame, " + std::to_string(unescaped.size()) + " bytes");
        InterfaceImpl::handle_incoming(unescaped);
    }
}

/*virtual*/ void TCPClientInterface::send_outgoing(const Bytes& data) {
    DEBUG(toString() + ".send_outgoing: data: " + std::to_string(data.size()) + " bytes");

    if (!_online) {
        DEBUG("TCPClientInterface: Not connected, cannot send");
        return;
    }

    try {
        // Frame with HDLC
        Bytes framed = HDLC::frame(data);

#ifdef ARDUINO
        size_t written = _client.write(framed.data(), framed.size());
        if (written != framed.size()) {
            ERROR("TCPClientInterface: Write incomplete, " + std::to_string(written) +
                  " of " + std::to_string(framed.size()) + " bytes");
            handle_disconnect();
            return;
        }
        _client.flush();
#else
        ssize_t written = send(_socket, framed.data(), framed.size(), MSG_NOSIGNAL);
        if (written < 0) {
            ERROR("TCPClientInterface: send error " + std::to_string(errno));
            handle_disconnect();
            return;
        }
        if (static_cast<size_t>(written) != framed.size()) {
            ERROR("TCPClientInterface: Write incomplete, " + std::to_string(written) +
                  " of " + std::to_string(framed.size()) + " bytes");
            handle_disconnect();
            return;
        }
#endif

        // Perform post-send housekeeping
        InterfaceImpl::handle_outgoing(data);

    } catch (std::exception& e) {
        ERROR("TCPClientInterface: Exception during send: " + std::string(e.what()));
        handle_disconnect();
    }
}
