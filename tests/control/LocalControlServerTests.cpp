#include "control/LocalControlServer.hpp"

#include <cerrno>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

int failures = 0;
int assertions = 0;

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        ++assertions;                                                                     \
        if (!(condition)) {                                                               \
            ++failures;                                                                   \
            std::cerr << __func__ << ":" << __LINE__ << ": CHECK failed: "             \
                      << #condition << "\n";                                              \
            return false;                                                                 \
        }                                                                                 \
    } while (0)

class TempDirectory {
public:
    TempDirectory() {
        char pattern[] = "/tmp/vosc-local-control-XXXXXX";
        char* made = ::mkdtemp(pattern);
        if (made == nullptr) {
            throw std::runtime_error(std::strerror(errno));
        }
        path_ = made;
        if (::chmod(path_.c_str(), 0700) != 0) {
            throw std::runtime_error(std::strerror(errno));
        }
    }

    ~TempDirectory() {
        ::unlink(socketPath().c_str());
        ::unlink((path_ + "/existing").c_str());
        ::unlink((path_ + "/replacement").c_str());
        ::rmdir(path_.c_str());
    }

    const std::string& path() const { return path_; }
    std::string socketPath() const { return path_ + "/control.sock"; }

private:
    std::string path_;
};

class ClientSocket {
public:
    ClientSocket() = default;
    explicit ClientSocket(const std::string& path) { connectTo(path); }
    ~ClientSocket() { close(); }

    ClientSocket(const ClientSocket&) = delete;
    ClientSocket& operator=(const ClientSocket&) = delete;

    ClientSocket(ClientSocket&& other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    ClientSocket& operator=(ClientSocket&& other) noexcept {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }

    void connectTo(const std::string& path) {
        fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd_ < 0) {
            throw std::runtime_error(std::strerror(errno));
        }
        sockaddr_un address {};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd_, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0) {
            const std::string message = std::strerror(errno);
            close();
            throw std::runtime_error(message);
        }
        const int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) != 0) {
            throw std::runtime_error(std::strerror(errno));
        }
    }

    void sendBytes(const std::string& bytes) {
        std::size_t offset = 0;
        while (offset < bytes.size()) {
            const ssize_t sent = ::send(fd_, bytes.data() + offset, bytes.size() - offset, 0);
            if (sent > 0) {
                offset += static_cast<std::size_t>(sent);
            }
            else if (sent < 0 && errno == EINTR) {
                continue;
            }
            else {
                throw std::runtime_error(std::string("send: ") + std::strerror(errno));
            }
        }
    }

    std::size_t sendAvailable(const char* bytes, std::size_t size) {
        const ssize_t sent = ::send(fd_, bytes, size, 0);
        if (sent > 0) {
            return static_cast<std::size_t>(sent);
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            return 0;
        }
        throw std::runtime_error(std::string("send: ") + std::strerror(errno));
    }

    std::string receiveAvailable(bool* sawEof = nullptr) {
        std::string result;
        char buffer[65536];
        while (true) {
            const ssize_t received = ::recv(fd_, buffer, sizeof(buffer), 0);
            if (received > 0) {
                result.append(buffer, static_cast<std::size_t>(received));
                continue;
            }
            if (received == 0) {
                if (sawEof != nullptr) {
                    *sawEof = true;
                }
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            throw std::runtime_error(std::string("recv: ") + std::strerror(errno));
        }
        return result;
    }

    void halfClose() { ::shutdown(fd_, SHUT_WR); }
    void close() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

std::string pumpUntilLine(control::LocalControlServer& server, ClientSocket& client,
                          const control::LocalControlServer::Handler& handler,
                          double& now, int maxUpdates = 20) {
    std::string received;
    for (int i = 0; i < maxUpdates && received.find('\n') == std::string::npos; ++i) {
        server.update(handler, now);
        now += 0.01;
        received += client.receiveAvailable();
    }
    return received;
}

bool testLifecycleAndSocketPermissions() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    CHECK(server.isRunning());

    struct stat socketInfo {};
    CHECK(::lstat(directory.socketPath().c_str(), &socketInfo) == 0);
    CHECK(S_ISSOCK(socketInfo.st_mode));
    CHECK((socketInfo.st_mode & 0777) == 0600);

    server.stop();
    CHECK(!server.isRunning());
    CHECK(::lstat(directory.socketPath().c_str(), &socketInfo) != 0 && errno == ENOENT);
    return true;
}

bool testFragmentedCoalescedAndCrLfFraming() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    ClientSocket client(directory.socketPath());
    std::vector<std::string> requests;
    const auto handler = [&](const std::string& request) {
        requests.push_back(request);
        return std::string("reply:") + request;
    };
    double now = 0.0;

    client.sendBytes("fir");
    server.update(handler, now);
    CHECK(requests.empty());
    client.sendBytes("st\r\nsecond\n");
    now += 0.01;
    server.update(handler, now);
    CHECK(requests.size() == 1 && requests[0] == "first");
    CHECK(client.receiveAvailable() == "reply:first\n");
    now += 0.01;
    server.update(handler, now);
    CHECK(requests.size() == 2 && requests[1] == "second");
    CHECK(client.receiveAvailable() == "reply:second\n");
    return true;
}

bool testHalfCloseFlushesAllCompleteRequests() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    ClientSocket client(directory.socketPath());
    client.sendBytes("one\ntwo\ntrailing-partial");
    client.halfClose();
    const auto handler = [](const std::string& request) { return "ok:" + request; };
    std::string received;
    bool eof = false;
    for (int i = 0; i < 12 && !eof; ++i) {
        server.update(handler, i * 0.01);
        received += client.receiveAvailable(&eof);
    }
    CHECK(received == "ok:one\nok:two\n");
    CHECK(eof);
    CHECK(server.clientCount() == 0);
    return true;
}

bool testClientLimitIsEnforced() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    std::vector<ClientSocket> clients;
    for (int i = 0; i < 8; ++i) {
        clients.emplace_back(directory.socketPath());
    }
    const auto handler = [](const std::string&) { return std::string("ok"); };
    server.update(handler, 0.0);
    CHECK(server.clientCount() == 8);

    ClientSocket extra(directory.socketPath());
    server.update(handler, 0.1);
    CHECK(server.clientCount() == 8);
    bool extraClosed = false;
    (void) extra.receiveAvailable(&extraClosed);
    CHECK(extraClosed);
    return true;
}

bool testInputLimitClosesOversizedClient() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    ClientSocket client(directory.socketPath());
    const std::string oversized(256U * 1024U + 1U, 'x');
    const auto handler = [](const std::string&) { return std::string("unexpected"); };
    std::size_t sent = 0;
    double now = 0.0;
    while (sent < oversized.size()) {
        const std::size_t amount = client.sendAvailable(oversized.data() + sent,
                                                        oversized.size() - sent);
        sent += amount;
        if (amount == 0) {
            server.update(handler, now);
            now += 0.01;
        }
    }
    while (server.clientCount() != 0 && now < 1.0) {
        server.update(handler, now);
        now += 0.01;
    }
    CHECK(server.clientCount() == 0);
    bool eof = false;
    (void) client.receiveAvailable(&eof);
    CHECK(eof);
    return true;
}

bool testOutputLimitClosesClient() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    ClientSocket client(directory.socketPath());
    client.sendBytes("large\n");
    const std::string tooLarge(8U * 1024U * 1024U + 1U, 'y');
    server.update([&](const std::string&) { return tooLarge; }, 0.0);
    CHECK(server.clientCount() == 0);
    bool eof = false;
    (void) client.receiveAvailable(&eof);
    CHECK(eof);
    return true;
}

bool testStalledClientDoesNotBlockAnotherClient() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    ClientSocket stalled(directory.socketPath());
    ClientSocket quick(directory.socketPath());
    stalled.sendBytes("slow\n");
    quick.sendBytes("quick\n");
    const std::string largeReply(1024U * 1024U, 's');
    const auto handler = [&](const std::string& request) {
        return request == "slow" ? largeReply : std::string("fast");
    };
    double now = 0.0;
    const std::string quickReply = pumpUntilLine(server, quick, handler, now, 4);
    CHECK(quickReply == "fast\n");
    CHECK(server.clientCount() == 2);
    return true;
}

bool testDisconnectedClientIsRemoved() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    ClientSocket client(directory.socketPath());
    server.update([](const std::string&) { return std::string("ok"); }, 0.0);
    CHECK(server.clientCount() == 1);
    client.close();
    server.update([](const std::string&) { return std::string("ok"); }, 0.1);
    CHECK(server.clientCount() == 0);
    return true;
}

bool testHandlerExceptionReturnsStructuredError() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    ClientSocket client(directory.socketPath());
    client.sendBytes("explode\n");
    double now = 0.0;
    const std::string response = pumpUntilLine(
        server, client,
        [](const std::string&) -> std::string { throw std::runtime_error("secret detail"); },
        now);
    CHECK(response.find("\"code\":\"internal_error\"") != std::string::npos);
    CHECK(response.find("secret detail") == std::string::npos);
    CHECK(!response.empty() && response.back() == '\n');
    return true;
}

bool testIdleTimeoutUsesCallerClock() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    ClientSocket client(directory.socketPath());
    const auto handler = [](const std::string&) { return std::string("ok"); };
    server.update(handler, 100.0);
    CHECK(server.clientCount() == 1);
    server.update(handler, 109.999);
    CHECK(server.clientCount() == 1);
    server.update(handler, 110.0);
    CHECK(server.clientCount() == 0);
    return true;
}

bool testPrivateParentAndExistingPathSafety() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(::chmod(directory.path().c_str(), 0750) == 0);
    CHECK(!server.start(directory.socketPath()));
    CHECK(!server.getLastError().empty());
    CHECK(::chmod(directory.path().c_str(), 0700) == 0);

    const std::string existing = directory.path() + "/existing";
    const int fd = ::open(existing.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    CHECK(fd >= 0);
    CHECK(::write(fd, "keep", 4) == 4);
    ::close(fd);
    CHECK(!server.start(existing));
    char contents[5] = {};
    const int readFd = ::open(existing.c_str(), O_RDONLY);
    CHECK(readFd >= 0);
    CHECK(::read(readFd, contents, 4) == 4);
    ::close(readFd);
    CHECK(std::string(contents) == "keep");

    CHECK(server.start(directory.socketPath()));
    control::LocalControlServer secondServer;
    CHECK(!secondServer.start(directory.socketPath()));
    CHECK(server.isRunning());
    struct stat originalSocket {};
    CHECK(::lstat(directory.socketPath().c_str(), &originalSocket) == 0);
    CHECK(S_ISSOCK(originalSocket.st_mode));
    return true;
}

bool testReplacementPathSurvivesStop() {
    TempDirectory directory;
    control::LocalControlServer server;
    CHECK(server.start(directory.socketPath()));
    CHECK(::unlink(directory.socketPath().c_str()) == 0);
    const int fd = ::open(directory.socketPath().c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0600);
    CHECK(fd >= 0);
    CHECK(::write(fd, "replacement", 11) == 11);
    ::close(fd);
    server.stop();

    struct stat replacement {};
    CHECK(::lstat(directory.socketPath().c_str(), &replacement) == 0);
    CHECK(S_ISREG(replacement.st_mode));
    char contents[12] = {};
    const int readFd = ::open(directory.socketPath().c_str(), O_RDONLY);
    CHECK(readFd >= 0);
    CHECK(::read(readFd, contents, 11) == 11);
    ::close(readFd);
    CHECK(std::string(contents) == "replacement");
    return true;
}

bool testDestructorRemovesOwnedSocket() {
    TempDirectory directory;
    {
        control::LocalControlServer server;
        CHECK(server.start(directory.socketPath()));
    }
    struct stat info {};
    CHECK(::lstat(directory.socketPath().c_str(), &info) != 0 && errno == ENOENT);
    return true;
}

} // namespace

int main() {
    using Test = std::pair<const char*, std::function<bool()>>;
    const std::vector<Test> tests = {
        {"lifecycle and socket permissions", testLifecycleAndSocketPermissions},
        {"fragmented, coalesced, CRLF framing", testFragmentedCoalescedAndCrLfFraming},
        {"half-close response flushing", testHalfCloseFlushesAllCompleteRequests},
        {"client limit", testClientLimitIsEnforced},
        {"input bound", testInputLimitClosesOversizedClient},
        {"output bound", testOutputLimitClosesClient},
        {"stalled client fairness", testStalledClientDoesNotBlockAnotherClient},
        {"disconnected client", testDisconnectedClientIsRemoved},
        {"handler exception", testHandlerExceptionReturnsStructuredError},
        {"idle timeout", testIdleTimeoutUsesCallerClock},
        {"private parent and existing path", testPrivateParentAndExistingPathSafety},
        {"replacement cleanup identity", testReplacementPathSurvivesStop},
        {"destructor cleanup", testDestructorRemovesOwnedSocket},
    };

    int passed = 0;
    for (const Test& test : tests) {
        try {
            if (test.second()) {
                ++passed;
            }
            else {
                std::cerr << "FAILED: " << test.first << "\n";
            }
        }
        catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAILED: " << test.first << ": " << error.what() << "\n";
        }
    }

    std::cout << "LocalControlServerTests: " << passed << "/" << tests.size()
              << " tests passed, " << assertions << " assertions, " << failures
              << " failures\n";
    return failures == 0 && passed == static_cast<int>(tests.size()) ? 0 : 1;
}
