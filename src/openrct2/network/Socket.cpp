/*****************************************************************************
 * Copyright (c) 2014-2020 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#ifndef DISABLE_NETWORK

#    include <atomic>
#    include <chrono>
#    include <cmath>
#    include <cstring>
#    include <future>
#    include <string>
#    include <thread>

// clang-format off
// MSVC: include <math.h> here otherwise PI gets defined twice
#include <cmath>

#ifdef _WIN32
    // winsock2 must be included before windows.h
    #include <winsock2.h>
    #include <ws2tcpip.h>

    #define LAST_SOCKET_ERROR() WSAGetLastError()
    #undef EWOULDBLOCK
    #define EWOULDBLOCK WSAEWOULDBLOCK
    #ifndef SHUT_RD
        #define SHUT_RD SD_RECEIVE
    #endif
    #ifndef SHUT_RDWR
        #define SHUT_RDWR SD_BOTH
    #endif
    #define FLAG_NO_PIPE 0
#else
    #include <arpa/inet.h>
    #include <cerrno>
    #include <fcntl.h>
    #include <net/if.h>
    #include <netdb.h>
    #include <netinet/in.h>
    #include <netinet/tcp.h>
    #include <sys/ioctl.h>
    #include <sys/socket.h>
    #include "../common.h"
    using SOCKET = int32_t;
    #define SOCKET_ERROR -1
    #define INVALID_SOCKET -1
    #define LAST_SOCKET_ERROR() errno
    #define closesocket close
    #define ioctlsocket ioctl
    #if defined(__linux__)
        #define FLAG_NO_PIPE MSG_NOSIGNAL
    #else
        #define FLAG_NO_PIPE 0
    #endif // defined(__linux__)
#endif // _WIN32
// clang-format on

#    include "Socket.h"

constexpr auto CONNECT_TIMEOUT = std::chrono::milliseconds(3000);

#    ifdef _WIN32
static bool _wsaInitialised = false;
#    endif

class SocketException : public std::runtime_error
{
public:
    explicit SocketException(const std::string& message)
        : std::runtime_error(message)
    {
    }
};

class NetworkEndpoint final : public INetworkEndpoint
{
private:
    sockaddr _address{};
    socklen_t _addressLen{};

public:
    NetworkEndpoint()
    {
    }

    NetworkEndpoint(const sockaddr* address, socklen_t addressLen)
    {
        std::memcpy(&_address, address, addressLen);
        _addressLen = addressLen;
    }

    const sockaddr& GetAddress() const
    {
        return _address;
    }

    socklen_t GetAddressLen() const
    {
        return _addressLen;
    }

    int32_t GetPort() const
    {
        if (_address.sa_family == AF_INET)
        {
            return reinterpret_cast<const sockaddr_in*>(&_address)->sin_port;
        }
        else
        {
            return reinterpret_cast<const sockaddr_in6*>(&_address)->sin6_port;
        }
    }

    std::string GetHostname() const override
    {
        char hostname[256]{};
        int res = getnameinfo(&_address, _addressLen, hostname, sizeof(hostname), nullptr, 0, NI_NUMERICHOST);
        if (res == 0)
        {
            return hostname;
        }
        return {};
    }
};

class Socket
{
protected:
    static bool ResolveAddress(const std::string& address, uint16_t port, sockaddr_storage* ss, socklen_t* ss_len)
    {
        return ResolveAddress(AF_UNSPEC, address, port, ss, ss_len);
    }

    static bool ResolveAddressIPv4(const std::string& address, uint16_t port, sockaddr_storage* ss, socklen_t* ss_len)
    {
        return ResolveAddress(AF_INET, address, port, ss, ss_len);
    }

    static bool SetNonBlocking(SOCKET socket, bool on)
    {
#    ifdef _WIN32
        u_long nonBlocking = on;
        return ioctlsocket(socket, FIONBIO, &nonBlocking) == 0;
#    else
        int32_t flags = fcntl(socket, F_GETFL, 0);
        return fcntl(socket, F_SETFL, on ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK)) == 0;
#    endif
    }

    static bool SetOption(SOCKET socket, int32_t a, int32_t b, bool value)
    {
        int32_t ivalue = value ? 1 : 0;
        return setsockopt(socket, a, b, reinterpret_cast<const char*>(&ivalue), sizeof(ivalue)) == 0;
    }

private:
    static bool ResolveAddress(
        int32_t family, const std::string& address, uint16_t port, sockaddr_storage* ss, socklen_t* ss_len)
    {
        std::string serviceName = std::to_string(port);

        addrinfo hints = {};
        hints.ai_family = family;
        if (address.empty())
        {
            hints.ai_flags = AI_PASSIVE;
        }

        addrinfo* result = nullptr;
        int errorcode = getaddrinfo(address.empty() ? nullptr : address.c_str(), serviceName.c_str(), &hints, &result);
        if (errorcode != 0)
        {
            log_error("Resolving address failed: Code %d.", errorcode);
            log_error("Resolution error message: %s.", gai_strerror(errorcode));
            return false;
        }
        if (result == nullptr)
        {
            return false;
        }
        else
        {
            std::memcpy(ss, result->ai_addr, result->ai_addrlen);
            *ss_len = static_cast<socklen_t>(result->ai_addrlen);
            freeaddrinfo(result);
            return true;
        }
    }
};

class TcpSocket final : public ITcpSocket, protected Socket
{
private:
    std::atomic<SOCKET_STATUS> _status = ATOMIC_VAR_INIT(SOCKET_STATUS_CLOSED);
    uint16_t _listeningPort = 0;
    SOCKET _socket = INVALID_SOCKET;

    std::string _ipAddress;
    std::string _hostName;
    std::future<void> _connectFuture;
    std::string _error;

public:
    TcpSocket() = default;

    ~TcpSocket() override
    {
        if (_connectFuture.valid())
        {
            _connectFuture.wait();
        }
        CloseSocket();
    }

    SOCKET_STATUS GetStatus() const override
    {
        return _status;
    }

    const char* GetError() const override
    {
        return _error.empty() ? nullptr : _error.c_str();
    }

    void Listen(uint16_t port) override
    {
        Listen("", port);
    }

    void Listen(const std::string& address, uint16_t port) override
    {
        if (_status != SOCKET_STATUS_CLOSED)
        {
            throw std::runtime_error("Socket not closed.");
        }

        sockaddr_storage ss{};
        socklen_t ss_len;
        if (!ResolveAddress(address, port, &ss, &ss_len))
        {
            throw SocketException("Unable to resolve address.");
        }

        // Create the listening socket
        _socket = socket(ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
        if (_socket == INVALID_SOCKET)
        {
            throw SocketException("Unable to create socket.");
        }

        // Turn off IPV6_V6ONLY so we can accept both v4 and v6 connections
        int32_t value = 0;
        if (setsockopt(_socket, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
        {
            log_error("IPV6_V6ONLY failed. %d", LAST_SOCKET_ERROR());
        }

        value = 1;
        if (setsockopt(_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value)) != 0)
        {
            log_error("SO_REUSEADDR failed. %d", LAST_SOCKET_ERROR());
        }

        try
        {
            // Bind to address:port and listen
            if (bind(_socket, reinterpret_cast<sockaddr*>(&ss), ss_len) != 0)
            {
                throw SocketException("Unable to bind to socket.");
            }
            if (listen(_socket, SOMAXCONN) != 0)
            {
                throw SocketException("Unable to listen on socket.");
            }

            if (!SetNonBlocking(_socket, true))
            {
                throw SocketException("Failed to set non-blocking mode.");
            }
        }
        catch (const std::exception&)
        {
            CloseSocket();
            throw;
        }

        _listeningPort = port;
        _status = SOCKET_STATUS_LISTENING;
    }

    std::unique_ptr<ITcpSocket> Accept() override
    {
        if (_status != SOCKET_STATUS_LISTENING)
        {
            throw std::runtime_error("Socket not listening.");
        }
        struct sockaddr_storage client_addr
        {
        };
        socklen_t client_len = sizeof(struct sockaddr_storage);

        std::unique_ptr<ITcpSocket> tcpSocket;
        SOCKET socket = accept(_socket, reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
        if (socket == INVALID_SOCKET)
        {
            if (LAST_SOCKET_ERROR() != EWOULDBLOCK)
            {
                log_error("Failed to accept client.");
            }
        }
        else
        {
            if (!SetNonBlocking(socket, true))
            {
                closesocket(socket);
                log_error("Failed to set non-blocking mode.");
            }
            else
            {
                auto ipAddress = GetIpAddressFromSocket(reinterpret_cast<sockaddr_in*>(&client_addr));

                char hostName[NI_MAXHOST];
                int32_t rc = getnameinfo(
                    reinterpret_cast<struct sockaddr*>(&client_addr), client_len, hostName, sizeof(hostName), nullptr, 0,
                    NI_NUMERICHOST | NI_NUMERICSERV);
                SetOption(socket, IPPROTO_TCP, TCP_NODELAY, true);

                if (rc == 0)
                {
                    tcpSocket = std::unique_ptr<ITcpSocket>(new TcpSocket(socket, hostName, ipAddress));
                }
                else
                {
                    tcpSocket = std::unique_ptr<ITcpSocket>(new TcpSocket(socket, "", ipAddress));
                }
            }
        }
        return tcpSocket;
    }

    void Connect(const std::string& address, uint16_t port) override
    {
        if (_status != SOCKET_STATUS_CLOSED)
        {
            throw std::runtime_error("Socket not closed.");
        }

        try
        {
            // Resolve address
            _status = SOCKET_STATUS_RESOLVING;

            sockaddr_storage ss{};
            socklen_t ss_len;
            if (!ResolveAddress(address, port, &ss, &ss_len))
            {
                throw SocketException("Unable to resolve address.");
            }

            _status = SOCKET_STATUS_CONNECTING;
            _socket = socket(ss.ss_family, SOCK_STREAM, IPPROTO_TCP);
            if (_socket == INVALID_SOCKET)
            {
                throw SocketException("Unable to create socket.");
            }

            SetOption(_socket, IPPROTO_TCP, TCP_NODELAY, true);
            if (!SetNonBlocking(_socket, true))
            {
                throw SocketException("Failed to set non-blocking mode.");
            }

            // Connect
            int32_t connectResult = connect(_socket, reinterpret_cast<sockaddr*>(&ss), ss_len);
            if (connectResult != SOCKET_ERROR || (LAST_SOCKET_ERROR() != EINPROGRESS && LAST_SOCKET_ERROR() != EWOULDBLOCK))
            {
                throw SocketException("Failed to connect.");
            }

            auto connectStartTime = std::chrono::system_clock::now();

            int32_t error = 0;
            socklen_t len = sizeof(error);
            if (getsockopt(_socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) != 0)
            {
                throw SocketException("getsockopt failed with error: " + std::to_string(LAST_SOCKET_ERROR()));
            }
            if (error != 0)
            {
                throw SocketException("Connection failed: " + std::to_string(error));
            }

            do
            {
                // Sleep for a bit
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                fd_set writeFD;
                FD_ZERO(&writeFD);
#    pragma warning(push)
#    pragma warning(disable : 4548) // expression before comma has no effect; expected expression with side-effect
                FD_SET(_socket, &writeFD);
#    pragma warning(pop)
                timeval timeout{};
                timeout.tv_sec = 0;
                timeout.tv_usec = 0;
                if (select(static_cast<int32_t>(_socket + 1), nullptr, &writeFD, nullptr, &timeout) > 0)
                {
                    error = 0;
                    len = sizeof(error);
                    if (getsockopt(_socket, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len) != 0)
                    {
                        throw SocketException("getsockopt failed with error: " + std::to_string(LAST_SOCKET_ERROR()));
                    }
                    if (error == 0)
                    {
                        _status = SOCKET_STATUS_CONNECTED;
                        return;
                    }
                }
            } while ((std::chrono::system_clock::now() - connectStartTime) < CONNECT_TIMEOUT);

            // Connection request timed out
            throw SocketException("Connection timed out.");
        }
        catch (const std::exception&)
        {
            CloseSocket();
            throw;
        }
    }

    void ConnectAsync(const std::string& address, uint16_t port) override
    {
        if (_status != SOCKET_STATUS_CLOSED)
        {
            throw std::runtime_error("Socket not closed.");
        }

        auto saddress = std::string(address);
        std::promise<void> barrier;
        _connectFuture = barrier.get_future();
        auto thread = std::thread(
            [this, saddress, port](std::promise<void> barrier2) -> void {
                try
                {
                    Connect(saddress.c_str(), port);
                }
                catch (const std::exception& ex)
                {
                    _error = std::string(ex.what());
                }
                barrier2.set_value();
            },
            std::move(barrier));
        thread.detach();
    }

    void Disconnect() override
    {
        if (_status == SOCKET_STATUS_CONNECTED)
        {
            shutdown(_socket, SHUT_RDWR);
        }
    }

    size_t SendData(const void* buffer, size_t size) override
    {
        if (_status != SOCKET_STATUS_CONNECTED)
        {
            throw std::runtime_error("Socket not connected.");
        }

        size_t totalSent = 0;
        do
        {
            const char* bufferStart = static_cast<const char*>(buffer) + totalSent;
            size_t remainingSize = size - totalSent;
            int32_t sentBytes = send(_socket, bufferStart, static_cast<int32_t>(remainingSize), FLAG_NO_PIPE);
            if (sentBytes == SOCKET_ERROR)
            {
                return totalSent;
            }
            totalSent += sentBytes;
        } while (totalSent < size);
        return totalSent;
    }

    NETWORK_READPACKET ReceiveData(void* buffer, size_t size, size_t* sizeReceived) override
    {
        if (_status != SOCKET_STATUS_CONNECTED)
        {
            throw std::runtime_error("Socket not connected.");
        }

        int32_t readBytes = recv(_socket, static_cast<char*>(buffer), static_cast<int32_t>(size), 0);
        if (readBytes == 0)
        {
            *sizeReceived = 0;
            return NETWORK_READPACKET_DISCONNECTED;
        }
        else if (readBytes == SOCKET_ERROR)
        {
            *sizeReceived = 0;
#    ifndef _WIN32
            // Removing the check for EAGAIN and instead relying on the values being the same allows turning on of
            // -Wlogical-op warning.
            // This is not true on Windows, see:
            // * https://msdn.microsoft.com/en-us/library/windows/desktop/ms737828(v=vs.85).aspx
            // * https://msdn.microsoft.com/en-us/library/windows/desktop/ms741580(v=vs.85).aspx
            // * https://msdn.microsoft.com/en-us/library/windows/desktop/ms740668(v=vs.85).aspx
            static_assert(
                EWOULDBLOCK == EAGAIN,
                "Portability note: your system has different values for EWOULDBLOCK "
                "and EAGAIN, please extend the condition below");
#    endif // _WIN32
            if (LAST_SOCKET_ERROR() != EWOULDBLOCK)
            {
                return NETWORK_READPACKET_DISCONNECTED;
            }
            else
            {
                return NETWORK_READPACKET_NO_DATA;
            }
        }
        else
        {
            *sizeReceived = readBytes;
            return NETWORK_READPACKET_SUCCESS;
        }
    }

    void Close() override
    {
        if (_connectFuture.valid())
        {
            _connectFuture.wait();
        }
        CloseSocket();
    }

    const char* GetHostName() const override
    {
        return _hostName.empty() ? nullptr : _hostName.c_str();
    }

    std::string GetIpAddress() const override
    {
        return _ipAddress;
    }

private:
    explicit TcpSocket(SOCKET socket, const std::string& hostName, const std::string& ipAddress)
    {
        _socket = socket;
        _hostName = hostName;
        _ipAddress = ipAddress;
        _status = SOCKET_STATUS_CONNECTED;
    }

    void CloseSocket()
    {
        if (_socket != INVALID_SOCKET)
        {
            closesocket(_socket);
            _socket = INVALID_SOCKET;
        }
        _status = SOCKET_STATUS_CLOSED;
    }

    std::string GetIpAddressFromSocket(const sockaddr_in* addr)
    {
        std::string result;
#    if defined(__MINGW32__)
        if (addr->sin_family == AF_INET)
        {
            result = inet_ntoa(addr->sin_addr);
        }
#    else
        if (addr->sin_family == AF_INET)
        {
            char str[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &addr->sin_addr, str, sizeof(str));
            result = str;
        }
        else if (addr->sin_family == AF_INET6)
        {
            auto addrv6 = reinterpret_cast<const sockaddr_in6*>(&addr);
            char str[INET6_ADDRSTRLEN]{};
            inet_ntop(AF_INET6, &addrv6->sin6_addr, str, sizeof(str));
            result = str;
        }
#    endif
        return result;
    }
};

class UdpSocket final : public IUdpSocket, protected Socket
{
private:
    SOCKET_STATUS _status = SOCKET_STATUS_CLOSED;
    uint16_t _listeningPort = 0;
    SOCKET _socket = INVALID_SOCKET;
    NetworkEndpoint _endpoint;

    std::string _hostName;
    std::string _error;

public:
    UdpSocket() = default;

    ~UdpSocket() override
    {
        CloseSocket();
    }

    SOCKET_STATUS GetStatus() const override
    {
        return _status;
    }

    const char* GetError() const override
    {
        return _error.empty() ? nullptr : _error.c_str();
    }

    void Listen(uint16_t port) override
    {
        Listen("", port);
    }

    void Listen(const std::string& address, uint16_t port) override
    {
        if (_status != SOCKET_STATUS_CLOSED)
        {
            throw std::runtime_error("Socket not closed.");
        }

        sockaddr_storage ss{};
        socklen_t ss_len;
        if (!ResolveAddressIPv4(address, port, &ss, &ss_len))
        {
            throw SocketException("Unable to resolve address.");
        }

        // Create the listening socket
        _socket = CreateSocket();
        try
        {
            // Bind to address:port and listen
            if (bind(_socket, reinterpret_cast<sockaddr*>(&ss), ss_len) != 0)
            {
                throw SocketException("Unable to bind to socket.");
            }
        }
        catch (const std::exception&)
        {
            CloseSocket();
            throw;
        }

        _listeningPort = port;
        _status = SOCKET_STATUS_LISTENING;
    }

    size_t SendData(const std::string& address, uint16_t port, const void* buffer, size_t size) override
    {
        sockaddr_storage ss{};
        socklen_t ss_len;
        if (!ResolveAddressIPv4(address, port, &ss, &ss_len))
        {
            throw SocketException("Unable to resolve address.");
        }
        NetworkEndpoint endpoint(reinterpret_cast<const sockaddr*>(&ss), ss_len);
        return SendData(endpoint, buffer, size);
    }

    size_t SendData(const INetworkEndpoint& destination, const void* buffer, size_t size) override
    {
        if (_socket == INVALID_SOCKET)
        {
            _socket = CreateSocket();
        }

        const auto& dest = dynamic_cast<const NetworkEndpoint*>(&destination);
        if (dest == nullptr)
        {
            throw std::invalid_argument("destination is not compatible.");
        }
        auto ss = &dest->GetAddress();
        auto ss_len = dest->GetAddressLen();

        if (_status != SOCKET_STATUS_LISTENING)
        {
            _endpoint = *dest;
        }

        size_t totalSent = 0;
        do
        {
            const char* bufferStart = static_cast<const char*>(buffer) + totalSent;
            size_t remainingSize = size - totalSent;
            int32_t sentBytes = sendto(
                _socket, bufferStart, static_cast<int32_t>(remainingSize), FLAG_NO_PIPE, static_cast<const sockaddr*>(ss),
                ss_len);
            if (sentBytes == SOCKET_ERROR)
            {
                return totalSent;
            }
            totalSent += sentBytes;
        } while (totalSent < size);
        return totalSent;
    }

    NETWORK_READPACKET ReceiveData(
        void* buffer, size_t size, size_t* sizeReceived, std::unique_ptr<INetworkEndpoint>* sender) override
    {
        sockaddr_in senderAddr{};
        socklen_t senderAddrLen = sizeof(sockaddr_in);
        if (_status != SOCKET_STATUS_LISTENING)
        {
            senderAddrLen = _endpoint.GetAddressLen();
            std::memcpy(&senderAddr, &_endpoint.GetAddress(), senderAddrLen);
        }
        auto readBytes = recvfrom(
            _socket, static_cast<char*>(buffer), static_cast<int32_t>(size), 0, reinterpret_cast<sockaddr*>(&senderAddr),
            &senderAddrLen);
        if (readBytes <= 0)
        {
            *sizeReceived = 0;
            return NETWORK_READPACKET_NO_DATA;
        }
        else
        {
            *sizeReceived = readBytes;
            if (sender != nullptr)
            {
                *sender = std::make_unique<NetworkEndpoint>(reinterpret_cast<sockaddr*>(&senderAddr), senderAddrLen);
            }
            return NETWORK_READPACKET_SUCCESS;
        }
    }

    void Close() override
    {
        CloseSocket();
    }

    const char* GetHostName() const override
    {
        return _hostName.empty() ? nullptr : _hostName.c_str();
    }

private:
    explicit UdpSocket(SOCKET socket, const std::string& hostName)
    {
        _socket = socket;
        _hostName = hostName;
        _status = SOCKET_STATUS_CONNECTED;
    }

    SOCKET CreateSocket()
    {
        auto sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock == INVALID_SOCKET)
        {
            throw SocketException("Unable to create socket.");
        }

        // Enable send and receiving of broadcast messages
        if (!SetOption(sock, SOL_SOCKET, SO_BROADCAST, true))
        {
            log_warning("SO_BROADCAST failed. %d", LAST_SOCKET_ERROR());
        }

        // Turn off IPV6_V6ONLY so we can accept both v4 and v6 connections
        if (!SetOption(sock, IPPROTO_IPV6, IPV6_V6ONLY, false))
        {
            log_warning("IPV6_V6ONLY failed. %d", LAST_SOCKET_ERROR());
        }

        if (!SetOption(sock, SOL_SOCKET, SO_REUSEADDR, true))
        {
            log_warning("SO_REUSEADDR failed. %d", LAST_SOCKET_ERROR());
        }

        if (!SetNonBlocking(sock, true))
        {
            throw SocketException("Failed to set non-blocking mode.");
        }

        return sock;
    }

    void CloseSocket()
    {
        if (_socket != INVALID_SOCKET)
        {
            closesocket(_socket);
            _socket = INVALID_SOCKET;
        }
        _status = SOCKET_STATUS_CLOSED;
    }
};

bool InitialiseWSA()
{
#    ifdef _WIN32
    if (!_wsaInitialised)
    {
        log_verbose("Initialising WSA");
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
        {
            log_error("Unable to initialise winsock.");
            return false;
        }
        _wsaInitialised = true;
    }
    return _wsaInitialised;
#    else
    return true;
#    endif
}

void DisposeWSA()
{
#    ifdef _WIN32
    if (_wsaInitialised)
    {
        WSACleanup();
        _wsaInitialised = false;
    }
#    endif
}

std::unique_ptr<ITcpSocket> CreateTcpSocket()
{
    return std::make_unique<TcpSocket>();
}

std::unique_ptr<IUdpSocket> CreateUdpSocket()
{
    return std::make_unique<UdpSocket>();
}

#    ifdef _WIN32
static std::vector<INTERFACE_INFO> GetNetworkInterfaces()
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        return {};
    }

    // Get all the network interfaces, requires a trial and error approch
    // until we find the capacity required to store all of them.
    DWORD len = 0;
    size_t capacity = 16;
    std::vector<INTERFACE_INFO> interfaces;
    for (;;)
    {
        interfaces.resize(capacity);
        if (WSAIoctl(
                sock, SIO_GET_INTERFACE_LIST, nullptr, 0, interfaces.data(), (DWORD)(capacity * sizeof(INTERFACE_INFO)), &len,
                nullptr, nullptr)
            == 0)
        {
            break;
        }
        if (WSAGetLastError() != WSAEFAULT)
        {
            closesocket(sock);
            return {};
        }
        capacity *= 2;
    }
    interfaces.resize(len / sizeof(INTERFACE_INFO));
    interfaces.shrink_to_fit();
    return interfaces;
}
#    endif

std::vector<std::unique_ptr<INetworkEndpoint>> GetBroadcastAddresses()
{
    std::vector<std::unique_ptr<INetworkEndpoint>> baddresses;
#    ifdef _WIN32
    auto interfaces = GetNetworkInterfaces();
    for (const auto& ifo : interfaces)
    {
        if (ifo.iiFlags & IFF_LOOPBACK)
            continue;
        if (!(ifo.iiFlags & IFF_BROADCAST))
            continue;

        // iiBroadcast is unusable, because it always seems to be set to 255.255.255.255.
        sockaddr_storage address{};
        memcpy(&address, &ifo.iiAddress.Address, sizeof(sockaddr));
        ((sockaddr_in*)&address)->sin_addr.s_addr = ifo.iiAddress.AddressIn.sin_addr.s_addr
            | ~ifo.iiNetmask.AddressIn.sin_addr.s_addr;
        baddresses.push_back(std::make_unique<NetworkEndpoint>((const sockaddr*)&address, (socklen_t)sizeof(sockaddr)));
    }
#    else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock == -1)
    {
        return baddresses;
    }

    char buf[4 * 1024]{};
    ifconf ifconfx{};
    ifconfx.ifc_len = sizeof(buf);
    ifconfx.ifc_buf = buf;
    if (ioctl(sock, SIOCGIFCONF, &ifconfx) == -1)
    {
        close(sock);
        return baddresses;
    }

    const char* buf_end = buf + ifconfx.ifc_len;
    for (const char* p = buf; p < buf_end;)
    {
        auto req = reinterpret_cast<const ifreq*>(p);
        if (req->ifr_addr.sa_family == AF_INET)
        {
            ifreq r;
            strcpy(r.ifr_name, req->ifr_name);
            if (ioctl(sock, SIOCGIFFLAGS, &r) != -1 && (r.ifr_flags & IFF_BROADCAST) && ioctl(sock, SIOCGIFBRDADDR, &r) != -1)
            {
                baddresses.push_back(std::make_unique<NetworkEndpoint>(&r.ifr_broadaddr, sizeof(sockaddr)));
            }
        }
        p += sizeof(ifreq);
#        if defined(AF_LINK) && !defined(SUNOS)
        p += req->ifr_addr.sa_len - sizeof(struct sockaddr);
#        endif
    }
    close(sock);
#    endif
    return baddresses;
}

namespace Convert
{
    uint16_t HostToNetwork(uint16_t value)
    {
        return htons(value);
    }

    uint16_t NetworkToHost(uint16_t value)
    {
        return ntohs(value);
    }
} // namespace Convert

#endif
