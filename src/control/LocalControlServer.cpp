#include "LocalControlServer.hpp"

#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#define VOSC_HAS_LOCAL_CONTROL_SOCKET 1
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#else
#define VOSC_HAS_LOCAL_CONTROL_SOCKET 0
#endif

namespace control {

namespace {

#if VOSC_HAS_LOCAL_CONTROL_SOCKET

constexpr std::size_t kMaxClients = 8;
constexpr std::size_t kMaxInputBytes = 256U * 1024U;
constexpr std::size_t kMaxOutputBytes = 8U * 1024U * 1024U;
constexpr std::size_t kIoChunkBytes = 64U * 1024U;
constexpr std::size_t kMaxAcceptsPerUpdate = 8;
constexpr double kIdleTimeoutSeconds = 10.0;

std::string systemError(const char* operation) {
    return std::string(operation) + ": " + std::strerror(errno);
}

bool setDescriptorFlags(int fd, std::string& error) {
    const int statusFlags = ::fcntl(fd, F_GETFL, 0);
    if (statusFlags < 0 || ::fcntl(fd, F_SETFL, statusFlags | O_NONBLOCK) < 0) {
        error = systemError("fcntl(O_NONBLOCK)");
        return false;
    }

    const int descriptorFlags = ::fcntl(fd, F_GETFD, 0);
    if (descriptorFlags < 0 || ::fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0) {
        error = systemError("fcntl(FD_CLOEXEC)");
        return false;
    }
    return true;
}

void closeDescriptor(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

std::string parentPathOf(const std::string& path) {
    const std::size_t slash = path.rfind('/');
    if (slash == 0) {
        return "/";
    }
    return path.substr(0, slash);
}

constexpr const char* kInternalErrorResponse =
    "{\"id\":null,\"ok\":false,\"status\":\"failed\","
    "\"error\":{\"code\":\"internal_error\","
    "\"message\":\"request handler failed\"}}";

#endif

} // namespace

struct LocalControlServer::Impl {
    std::string lastError;

#if VOSC_HAS_LOCAL_CONTROL_SOCKET
    struct Client {
        int fd = -1;
        std::string input;
        std::string output;
        std::size_t outputOffset = 0;
        double lastIoSeconds = 0.0;
        bool peerClosed = false;
        bool failed = false;
    };

    int listenFd = -1;
    std::string socketPath;
    dev_t socketDevice = 0;
    ino_t socketInode = 0;
    bool ownsSocketPath = false;
    std::vector<Client> clients;

    bool running() const {
        return listenFd >= 0;
    }

    void closeClient(Client& client) {
        closeDescriptor(client.fd);
        client.failed = true;
    }

    void stop() {
        for (Client& client : clients) {
            closeDescriptor(client.fd);
        }
        clients.clear();
        closeDescriptor(listenFd);

        if (ownsSocketPath && !socketPath.empty()) {
            struct stat current {};
            if (::lstat(socketPath.c_str(), &current) == 0 && S_ISSOCK(current.st_mode) &&
                current.st_dev == socketDevice && current.st_ino == socketInode) {
                if (::unlink(socketPath.c_str()) != 0) {
                    lastError = systemError("unlink socket");
                }
            }
        }

        ownsSocketPath = false;
        socketDevice = 0;
        socketInode = 0;
        socketPath.clear();
    }

    void acceptClients(double nowSeconds) {
        for (std::size_t attempt = 0; attempt < kMaxAcceptsPerUpdate; ++attempt) {
            int fd = ::accept(listenFd, nullptr, nullptr);
            if (fd < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                    lastError = systemError("accept");
                }
                break;
            }

            std::string flagError;
            if (!setDescriptorFlags(fd, flagError)) {
                ::close(fd);
                lastError = flagError;
                continue;
            }
#if defined(SO_NOSIGPIPE)
            const int enabled = 1;
            if (::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
                ::close(fd);
                lastError = systemError("setsockopt(SO_NOSIGPIPE)");
                continue;
            }
#endif
            if (clients.size() >= kMaxClients) {
                ::close(fd);
                continue;
            }

            Client client;
            client.fd = fd;
            client.lastIoSeconds = nowSeconds;
            clients.push_back(std::move(client));
        }
    }

    void receiveOnce(Client& client, double nowSeconds) {
        if (client.peerClosed || client.failed) {
            return;
        }

        char buffer[kIoChunkBytes];
        const ssize_t received = ::recv(client.fd, buffer, sizeof(buffer), 0);
        if (received > 0) {
            client.lastIoSeconds = nowSeconds;
            const std::size_t byteCount = static_cast<std::size_t>(received);
            // Keep one extra byte only for the delimiter of a maximum-size line.
            if (client.input.size() > kMaxInputBytes + 1U ||
                byteCount > kMaxInputBytes + 1U - client.input.size()) {
                closeClient(client);
                return;
            }
            client.input.append(buffer, byteCount);
            const std::size_t newline = client.input.find('\n');
            if ((newline == std::string::npos && client.input.size() > kMaxInputBytes) ||
                (newline != std::string::npos && newline > kMaxInputBytes)) {
                closeClient(client);
            }
            return;
        }
        if (received == 0) {
            client.peerClosed = true;
            return;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            closeClient(client);
        }
    }

    void dispatchOne(Client& client, const Handler& handler) {
        if (client.failed) {
            return;
        }

        const std::size_t newline = client.input.find('\n');
        if (newline == std::string::npos) {
            return;
        }
        if (newline > kMaxInputBytes) {
            closeClient(client);
            return;
        }

        std::string request = client.input.substr(0, newline);
        client.input.erase(0, newline + 1);
        if (!request.empty() && request.back() == '\r') {
            request.pop_back();
        }

        std::string response;
        try {
            response = handler ? handler(request) : kInternalErrorResponse;
        }
        catch (...) {
            response = kInternalErrorResponse;
        }

        const std::size_t pendingOutput = client.output.size() - client.outputOffset;
        if (response.size() > kMaxOutputBytes || pendingOutput > kMaxOutputBytes - response.size() ||
            pendingOutput + response.size() >= kMaxOutputBytes) {
            closeClient(client);
            return;
        }

        if (client.outputOffset != 0) {
            client.output.erase(0, client.outputOffset);
            client.outputOffset = 0;
        }
        client.output.append(response);
        client.output.push_back('\n');
    }

    void sendOnce(Client& client, double nowSeconds) {
        if (client.failed || client.outputOffset >= client.output.size()) {
            return;
        }

        const std::size_t remaining = client.output.size() - client.outputOffset;
        const std::size_t byteCount = remaining < kIoChunkBytes ? remaining : kIoChunkBytes;
        int flags = 0;
#if defined(MSG_NOSIGNAL)
        flags |= MSG_NOSIGNAL;
#endif
        const ssize_t sent = ::send(client.fd, client.output.data() + client.outputOffset,
                                    byteCount, flags);
        if (sent > 0) {
            client.lastIoSeconds = nowSeconds;
            client.outputOffset += static_cast<std::size_t>(sent);
            if (client.outputOffset == client.output.size()) {
                client.output.clear();
                client.outputOffset = 0;
            }
            return;
        }
        if (sent < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            closeClient(client);
        }
    }

    bool shouldRemove(const Client& client, double nowSeconds) const {
        if (client.failed) {
            return true;
        }
        const bool hasCompleteRequest = client.input.find('\n') != std::string::npos;
        if (client.peerClosed && !hasCompleteRequest && client.output.empty()) {
            return true;
        }
        return nowSeconds - client.lastIoSeconds >= kIdleTimeoutSeconds;
    }
#else
    bool running() const { return false; }
    void stop() {}
#endif
};

LocalControlServer::LocalControlServer()
    : impl_(new Impl()) {}

LocalControlServer::~LocalControlServer() {
    stop();
}

bool LocalControlServer::start(const std::string& absolutePath) {
    stop();
    impl_->lastError.clear();

#if VOSC_HAS_LOCAL_CONTROL_SOCKET
    if (absolutePath.empty() || absolutePath.front() != '/' || absolutePath.back() == '/') {
        impl_->lastError = "socket path must be an absolute file path";
        return false;
    }

    sockaddr_un address {};
    if (absolutePath.size() >= sizeof(address.sun_path)) {
        impl_->lastError = "socket path is too long";
        return false;
    }

    const std::string parentPath = parentPathOf(absolutePath);
    struct stat parent {};
    if (::stat(parentPath.c_str(), &parent) != 0) {
        impl_->lastError = systemError("stat socket parent");
        return false;
    }
    if (!S_ISDIR(parent.st_mode)) {
        impl_->lastError = "socket parent is not a directory";
        return false;
    }
    if (parent.st_uid != ::geteuid()) {
        impl_->lastError = "socket parent is not owned by the current user";
        return false;
    }
    if ((parent.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        impl_->lastError = "socket parent permissions must deny group and other access";
        return false;
    }

    struct stat existing {};
    if (::lstat(absolutePath.c_str(), &existing) == 0) {
        impl_->lastError = "socket path already exists";
        return false;
    }
    if (errno != ENOENT) {
        impl_->lastError = systemError("lstat socket path");
        return false;
    }

    int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        impl_->lastError = systemError("socket");
        return false;
    }
    if (!setDescriptorFlags(fd, impl_->lastError)) {
        ::close(fd);
        return false;
    }

    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, absolutePath.c_str(), absolutePath.size() + 1);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
        impl_->lastError = systemError("bind");
        ::close(fd);
        return false;
    }

    dev_t createdDevice = 0;
    ino_t createdInode = 0;
    bool createdIdentityKnown = false;
    struct stat initiallyCreated {};
    const int initialStatResult = ::lstat(absolutePath.c_str(), &initiallyCreated);
    if (initialStatResult == 0 && S_ISSOCK(initiallyCreated.st_mode)) {
        createdDevice = initiallyCreated.st_dev;
        createdInode = initiallyCreated.st_ino;
        createdIdentityKnown = true;
    }
    else {
        impl_->lastError = initialStatResult == 0 ? "bound path is not a socket"
                                                  : systemError("lstat created socket");
    }

    if (!createdIdentityKnown) {
        // Preserve any path that appeared in place of the socket between bind and inspection.
    }
    else if (::chmod(absolutePath.c_str(), S_IRUSR | S_IWUSR) != 0) {
        impl_->lastError = systemError("chmod socket");
    }
    else if (::listen(fd, static_cast<int>(kMaxClients)) != 0) {
        impl_->lastError = systemError("listen");
    }
    else {
        struct stat created {};
        if (::lstat(absolutePath.c_str(), &created) != 0) {
            impl_->lastError = systemError("lstat created socket");
        }
        else if (!S_ISSOCK(created.st_mode) || created.st_dev != createdDevice ||
                 created.st_ino != createdInode) {
            impl_->lastError = "bound socket path identity changed during startup";
        }
        else {
            impl_->listenFd = fd;
            impl_->socketPath = absolutePath;
            impl_->socketDevice = createdDevice;
            impl_->socketInode = createdInode;
            impl_->ownsSocketPath = true;
            return true;
        }
    }

    ::close(fd);
    if (createdIdentityKnown) {
        struct stat created {};
        if (::lstat(absolutePath.c_str(), &created) == 0 && S_ISSOCK(created.st_mode) &&
            created.st_dev == createdDevice && created.st_ino == createdInode) {
            ::unlink(absolutePath.c_str());
        }
    }
    return false;
#else
    (void) absolutePath;
    impl_->lastError = "local Unix control sockets are not supported on this platform";
    return false;
#endif
}

void LocalControlServer::update(const Handler& handler, double nowSeconds) {
#if VOSC_HAS_LOCAL_CONTROL_SOCKET
    if (!impl_->running()) {
        return;
    }

    impl_->acceptClients(nowSeconds);
    for (Impl::Client& client : impl_->clients) {
        impl_->receiveOnce(client, nowSeconds);
        impl_->dispatchOne(client, handler);
        impl_->sendOnce(client, nowSeconds);
    }

    auto& clients = impl_->clients;
    for (auto it = clients.begin(); it != clients.end();) {
        if (impl_->shouldRemove(*it, nowSeconds)) {
            closeDescriptor(it->fd);
            it = clients.erase(it);
        }
        else {
            ++it;
        }
    }
#else
    (void) handler;
    (void) nowSeconds;
#endif
}

void LocalControlServer::stop() {
    impl_->stop();
}

bool LocalControlServer::isRunning() const {
    return impl_->running();
}

std::string LocalControlServer::getLastError() const {
    return impl_->lastError;
}

std::size_t LocalControlServer::clientCount() const {
#if VOSC_HAS_LOCAL_CONTROL_SOCKET
    return impl_->clients.size();
#else
    return 0;
#endif
}

} // namespace control
