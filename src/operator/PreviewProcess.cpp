#include "PreviewProcess.hpp"

#if VOSC_OPERATOR_UI
#include "../control/OscPort.hpp"
#include "ofAppGLFWWindow.h"
#include "ofFileUtils.h"
#include "ofLog.h"
#include <GLFW/glfw3.h>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <spawn.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#if defined(__APPLE__)
#include <sys/spawn.h>
#include <crt_externs.h>
#define VOSC_PREVIEW_PROCESS 1
#elif defined(__linux__) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 34)
#define VOSC_PREVIEW_PROCESS 1
extern char** environ;
#endif
#endif

#ifndef VOSC_PREVIEW_PROCESS
#define VOSC_PREVIEW_PROCESS 0
#endif

namespace vosc::operator_ui {
namespace {

#if VOSC_PREVIEW_PROCESS
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
constexpr int childChannel = 3;
constexpr int childDirectory = 4;
constexpr char socketName[] = "control.sock";
constexpr std::string_view readyPrefix = "VOSC_SCENE_READY frame=";
constexpr size_t lineLimit = 4096;
#if defined(__APPLE__)
constexpr int safeSendFlags = 0; // Parent uses SO_NOSIGPIPE, child ignores SIGPIPE.
#else
constexpr int safeSendFlags = MSG_NOSIGNAL;
#endif

class Descriptor {
public:
    Descriptor() = default;
    explicit Descriptor(int descriptor) : value(descriptor) {}
    ~Descriptor() { reset(); }
    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;
    Descriptor(Descriptor&& other) noexcept : value(std::exchange(other.value, -1)) {}
    Descriptor& operator=(Descriptor&& other) noexcept {
        if (this != &other) reset(std::exchange(other.value, -1));
        return *this;
    }

    int get() const { return value; }
    void reset(int descriptor = -1) {
        if (value >= 0) ::close(value);
        value = descriptor;
    }

private:
    int value = -1;
};

void check(int error, const char* operation) {
    if (error) throw std::system_error(error, std::generic_category(), operation);
}

void setNonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        check(errno, "configure preview descriptor");
}

void prepareForSpawn(Descriptor& fd) {
    // Spawn actions target only descriptors 0 through 4.
    if (fd.get() <= childDirectory) {
        const int duplicateFd = ::fcntl(fd.get(), F_DUPFD_CLOEXEC, childDirectory + 1);
        if (duplicateFd < 0) check(errno, "duplicate preview descriptor");
        fd.reset(duplicateFd);
    } else if (::fcntl(fd.get(), F_SETFD, FD_CLOEXEC) < 0) {
        check(errno, "protect preview descriptor");
    }
}

bool sameDirectory(const fs::path& path, int fd) {
    struct stat identity {}, current {};
    return ::fstat(fd, &identity) == 0 && S_ISDIR(identity.st_mode) &&
        identity.st_uid == ::geteuid() && !(identity.st_mode & (S_IRWXG | S_IRWXO)) &&
        ::lstat(path.c_str(), &current) == 0 && identity.st_dev == current.st_dev &&
        identity.st_ino == current.st_ino;
}

void removeDirectory(const fs::path& path, int fd) {
    if (!sameDirectory(path, fd)) return;
    struct stat entry {};
    if (::fstatat(fd, socketName, &entry, AT_SYMLINK_NOFOLLOW) == 0 && S_ISSOCK(entry.st_mode))
        ::unlinkat(fd, socketName, 0);
    ::rmdir(path.c_str());
}

unsigned int choosePort() {
    auto socket = Descriptor(::socket(AF_INET, SOCK_DGRAM, 0));
    if (socket.get() < 0) check(errno, "allocate preview OSC port");
    sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) < 0)
        check(errno, "allocate preview OSC port");
    socklen_t length = sizeof(address);
    if (::getsockname(socket.get(), reinterpret_cast<sockaddr*>(&address), &length) < 0)
        check(errno, "read preview OSC port");
    const auto port = ntohs(address.sin_port);
    if (!port || port == control::oscPortFromEnvironment())
        throw std::runtime_error("Could not select a separate OSC port. Retry the preview.");
    return port;
}

class SpawnActions {
public:
    SpawnActions() {
        check(::posix_spawn_file_actions_init(&actions), "initialize preview spawn file actions");
    }
    ~SpawnActions() { ::posix_spawn_file_actions_destroy(&actions); }
    posix_spawn_file_actions_t actions {};
};

class SpawnAttributes {
public:
    SpawnAttributes() {
        check(::posix_spawnattr_init(&attributes), "initialize preview spawn attributes");
    }
    ~SpawnAttributes() { ::posix_spawnattr_destroy(&attributes); }
    posix_spawnattr_t attributes {};
};

std::vector<char*> cStrings(std::vector<std::string>& strings) {
    std::vector<char*> pointers;
    pointers.reserve(strings.size() + 1);
    for (auto& string : strings) pointers.push_back(string.data());
    pointers.push_back(nullptr);
    return pointers;
}

std::vector<std::string> childEnvironment(const fs::path& data, const fs::path& socket, unsigned int port) {
#if defined(__APPLE__)
    auto** env = *_NSGetEnviron();
#else
    auto** env = environ;
#endif
    std::vector<std::string> clean;
    for (; *env; ++env) {
        std::string_view entry(*env);
        // Ignore control, preview and recovery options inherited from the primary.
        if (entry.substr(0, 5) != "VOSC_") clean.emplace_back(entry);
    }
    clean.push_back("VOSC_OSC_PORT=" + std::to_string(port));
    clean.push_back("VOSC_CONTROL_SOCKET=" + socket.string());
    clean.push_back("VOSC_PREVIEW_DATA_ROOT=" + data.string());
    return clean;
}

GLFWwindow* nativeWindow() {
    auto* window = dynamic_cast<ofAppGLFWWindow*>(ofGetWindowPtr());
    return window ? window->getGLFWWindow() : nullptr;
}

std::string exitDetail(int status) {
    if (WIFSIGNALED(status)) return "Preview stopped with signal " + std::to_string(WTERMSIG(status)) + ".";
    return "Preview exited with status " + std::to_string(WEXITSTATUS(status)) + ".";
}
#endif

} // namespace

struct PreviewProcess::Process {
    State state = State::Idle;
    std::string title, diagnostic;
    unsigned int port = 0;
#if VOSC_PREVIEW_PROCESS
    pid_t pid = 0;
    Descriptor channel, output, directoryFd;
    fs::path directory;
    Clock::time_point deadline;
    std::string line, tail, launchError, readyLine;
    int readyFrame = -1;
    int stopSignal = 0;
    bool discardLine = false;
    bool failedStop = false;
    bool invalidReady = false;

    void cleanup() {
        channel.reset();
        output.reset();
        if (!directory.empty()) removeDirectory(directory, directoryFd.get());
        directory.clear();
        directoryFd.reset();
        port = 0;
    }

    void readOutput() {
        if (output.get() < 0) return;
        char bytes[4096];
        for (int readCount = 0; readCount < 16; ++readCount) {
            const auto count = ::read(output.get(), bytes, sizeof(bytes));
            if (count == 0) {
                if (!discardLine && !line.empty()) finishLine();
                line.clear();
                output.reset();
                return;
            }
            if (count < 0) {
                if (errno == EINTR) continue;
                return;
            }
            for (ssize_t i = 0; i < count; ++i) {
                const char character = bytes[i];
                if (character == '\n') {
                    if (!discardLine) finishLine();
                    line.clear();
                    discardLine = false;
                } else if (!discardLine) {
                    line += character;
                    if (line.size() >= lineLimit) discardLine = true;
                }
            }
        }
    }

    void finishLine() {
        if (line.compare(0, 6, "vosc: ") == 0) launchError = line;
        if (!line.empty()) tail = line;
    }

    void readReady() {
        if (state != State::Starting || readyFrame >= 0 || invalidReady) return;
        char bytes[lineLimit];
        const auto count = ::recv(channel.get(), bytes, sizeof(bytes), MSG_DONTWAIT);
        if (count <= 0) return;
        readyLine.append(bytes, static_cast<size_t>(count));
        const auto newline = readyLine.find('\n');
        if (newline == std::string::npos && readyLine.size() < lineLimit) return;
        if (newline != readyLine.size() - 1 || readyLine.size() > lineLimit ||
            readyLine.compare(0, readyPrefix.size(), readyPrefix) != 0) {
            invalidReady = true;
            return;
        }
        const char* end = readyLine.data() + newline;
        const char* begin = readyLine.data() + readyPrefix.size();
        if (begin >= end) {
            invalidReady = true;
            return;
        }
        int frame = -1;
        const auto parsed = std::from_chars(begin, end, frame);
        constexpr std::string_view titlePrefix = " title=";
        const std::string_view last(parsed.ptr, end - parsed.ptr);
        if (parsed.ec != std::errc() || frame < 0 || last.size() <= titlePrefix.size() ||
            last.substr(0, titlePrefix.size()) != titlePrefix) {
            invalidReady = true;
            return;
        }
        readyFrame = frame;
    }

    void beginStop(std::string reason, bool failed) {
        channel.reset(); // EOF requests normal shutdown without a signal.
        state = State::Stopping;
        diagnostic = std::move(reason);
        failedStop = failed;
        deadline = Clock::now() + 300ms;
    }
#endif
};

PreviewProcess::PreviewProcess() : process(std::make_unique<Process>()) {}
PreviewProcess::~PreviewProcess() { shutdown(); }

bool PreviewProcess::supported() { return VOSC_PREVIEW_PROCESS; }
PreviewProcess::State PreviewProcess::state() const { return process->state; }
const std::string& PreviewProcess::title() const { return process->title; }
const std::string& PreviewProcess::diagnostic() const { return process->diagnostic; }
unsigned int PreviewProcess::port() const { return process->port; }

void PreviewProcess::start(const std::filesystem::path& manifest, const std::string& title,
                           const std::map<std::string, std::string>& inputs) {
    if (state() != State::Idle && state() != State::Failed) return;
    auto& run = *process;
    run.title = title;
    run.diagnostic.clear();

#if VOSC_PREVIEW_PROCESS
    try {
        auto executable = fs::canonical(ofFilePath::getCurrentExePathFS());
        auto data = fs::canonical(ofToDataPath("", true));
        const auto scene = fs::canonical(manifest);
        if (!fs::is_regular_file(executable) || !fs::is_directory(data) || !fs::is_regular_file(scene))
            throw std::runtime_error("Preview executable, data directory or scene is missing.");

        run.port = choosePort();
        char path[] = "/tmp/vosc-preview.XXXXXX";
        if (!::mkdtemp(path)) check(errno, "create preview control directory");
        run.directory = path;
        const int fd = ::open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0) {
            const int error = errno;
            ::rmdir(path);
            check(error, "open preview control directory");
        }
        run.directoryFd.reset(fd);
        prepareForSpawn(run.directoryFd);

        int sockets[2];
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) < 0)
            check(errno, "create preview ownership channel");
        Descriptor parentSocket(sockets[0]), childSocket(sockets[1]);
        prepareForSpawn(parentSocket);
        prepareForSpawn(childSocket);
        setNonblocking(parentSocket.get());
        setNonblocking(childSocket.get());
#if defined(__APPLE__)
        const int noSignal = 1;
        if (::setsockopt(parentSocket.get(), SOL_SOCKET, SO_NOSIGPIPE, &noSignal, sizeof(noSignal)) < 0)
            check(errno, "configure preview ownership channel");
#endif

        int pipe[2];
        if (::pipe(pipe) < 0) check(errno, "capture preview diagnostics");
        Descriptor readEnd(pipe[0]), writeEnd(pipe[1]);
        prepareForSpawn(readEnd);
        prepareForSpawn(writeEnd);
        setNonblocking(readEnd.get());

        SpawnActions files;
        check(::posix_spawn_file_actions_addopen(&files.actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0),
              "set preview stdin");
        check(::posix_spawn_file_actions_adddup2(&files.actions, writeEnd.get(), STDOUT_FILENO),
              "set preview stdout");
        check(::posix_spawn_file_actions_adddup2(&files.actions, writeEnd.get(), STDERR_FILENO),
              "set preview stderr");
        check(::posix_spawn_file_actions_adddup2(&files.actions, childSocket.get(), childChannel),
              "pass preview ownership channel");
        check(::posix_spawn_file_actions_adddup2(&files.actions, run.directoryFd.get(), childDirectory),
              "pass preview control directory");

        SpawnAttributes attributes;
        short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#if defined(__APPLE__)
        flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#else
        check(::posix_spawn_file_actions_addclosefrom_np(&files.actions, childDirectory + 1),
              "isolate preview descriptors");
#endif
        sigset_t mask, defaults;
        sigemptyset(&mask);
        sigemptyset(&defaults);
        sigaddset(&defaults, SIGTERM);
        sigaddset(&defaults, SIGPIPE);
        check(::posix_spawnattr_setsigmask(&attributes.attributes, &mask), "set preview signal mask");
        check(::posix_spawnattr_setsigdefault(&attributes.attributes, &defaults), "set preview signal defaults");
        check(::posix_spawnattr_setflags(&attributes.attributes, flags), "set preview spawn flags");

        std::vector<std::string> args{executable.string(), "--scene", scene.string(), "--preview-child", "1"};
        for (const auto& [name, value] : inputs) {
            args.push_back("--scene-input");
            args.push_back(name + "=" + value);
        }
        auto env = childEnvironment(data, run.directory / socketName, run.port);
        auto argv = cStrings(args);
        auto envp = cStrings(env);
        pid_t pid = 0;
        check(::posix_spawn(&pid, executable.c_str(), &files.actions, &attributes.attributes,
                            argv.data(), envp.data()), "launch preview renderer");
        run.pid = pid;
        run.channel = std::move(parentSocket);
        run.output = std::move(readEnd);
        run.line.clear();
        run.tail.clear();
        run.launchError.clear();
        run.readyLine.clear();
        run.readyFrame = -1;
        run.stopSignal = 0;
        run.discardLine = false;
        run.failedStop = false;
        run.invalidReady = false;
        run.deadline = Clock::now() + 17s;
        run.state = State::Starting;
    } catch (const std::exception& error) {
        run.cleanup();
        run.state = State::Failed;
        run.diagnostic = error.what();
    }
#else
    (void)manifest;
    (void)inputs;
    run.state = State::Failed;
    run.diagnostic = "Separate preview windows are unavailable on this build.";
#endif
}

void PreviewProcess::tick() {
#if VOSC_PREVIEW_PROCESS
    auto& run = *process;
    if (!run.pid) return;
    run.readOutput();
    run.readReady();
    int status = 0;
    const auto result = ::waitpid(run.pid, &status, WNOHANG);
    if (result == run.pid || (result < 0 && errno == ECHILD)) {
        run.pid = 0; // Reaped (or already gone). This PID is no longer ours to signal.
        run.readOutput();
        run.readReady();
        const bool expectedStop = run.state == State::Stopping && !run.failedStop;
        const bool cleanClose = !run.failedStop && result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
            (run.state == State::Ready || run.readyFrame >= 0);
        if (expectedStop || cleanClose) {
            run.state = State::Idle;
            if (!expectedStop) run.diagnostic = "Preview closed.";
        } else {
            run.state = State::Failed;
            if (!run.failedStop) {
                run.diagnostic = result > 0 ? exitDetail(status) : "Preview process is no longer available.";
                const auto& detail = run.launchError.empty() ? run.tail : run.launchError;
                if (!detail.empty()) run.diagnostic += " " + detail;
            }
        }
        run.cleanup();
        return;
    }
    // On interruption, defer every action until ownership is checked again.
    if (result < 0) return;
    const auto now = Clock::now();
    if (run.state == State::Starting && run.readyFrame >= 0) {
        run.state = State::Ready;
        run.tail.clear();
        ofLogNotice("preview") << "PID " << run.pid << " composed frame " << run.readyFrame
                               << ", OSC 127.0.0.1:" << run.port
                               << ", control " << (run.directory / socketName).string();
    } else if (run.state == State::Starting && run.invalidReady) {
        run.beginStop("The preview returned an invalid first-frame receipt.", true);
    } else if (run.state == State::Starting && now >= run.deadline) {
        run.beginStop("Preview timed out before composing its first frame.", true);
    } else if (run.state == State::Stopping && now >= run.deadline) {
        if (run.stopSignal == SIGKILL) return;
        run.stopSignal = run.stopSignal == SIGTERM ? SIGKILL : SIGTERM;
        ::kill(run.pid, run.stopSignal);
        run.deadline = now + 500ms;
    }
#endif
}

void PreviewProcess::focus() {
#if VOSC_PREVIEW_PROCESS
    if (state() != State::Ready) return;
    const char command = 'f';
    ssize_t sent;
    do {
        sent = ::send(process->channel.get(), &command, 1, safeSendFlags);
    } while (sent < 0 && errno == EINTR);
#endif
}

void PreviewProcess::stop() {
#if VOSC_PREVIEW_PROCESS
    if (state() == State::Starting) process->beginStop("Preview canceled.", false);
    else if (state() == State::Ready) process->beginStop("Preview closed.", false);
#endif
}

void PreviewProcess::shutdown() {
#if VOSC_PREVIEW_PROCESS
    stop();
    const auto limit = Clock::now() + 1200ms;
    while (process->pid && Clock::now() < limit) {
        tick();
        if (process->pid) std::this_thread::sleep_for(10ms);
    }
#endif
}

PreviewChild::PreviewChild() {
#if VOSC_PREVIEW_PROCESS
    sockaddr_storage peer {};
    socklen_t peerSize = sizeof(peer);
    if (::getpeername(childChannel, reinterpret_cast<sockaddr*>(&peer), &peerSize) < 0 ||
        peer.ss_family != AF_UNIX)
        throw std::runtime_error("Preview ownership channel is missing.");
    const char* socket = std::getenv("VOSC_CONTROL_SOCKET");
    const fs::path controlPath = socket ? socket : "";
    directory = controlPath.parent_path();
    if (!controlPath.is_absolute() || controlPath.filename() != socketName ||
        !sameDirectory(directory, childDirectory))
        throw std::runtime_error("Preview control directory is invalid.");
    const char* data = std::getenv("VOSC_PREVIEW_DATA_ROOT");
    const fs::path root = data ? data : "";
    if (!root.is_absolute() || !fs::is_directory(root))
        throw std::runtime_error("Preview data directory is unavailable.");
    if (!std::getenv("VOSC_OSC_PORT") || !control::oscPortFromEnvironment())
        throw std::runtime_error("Preview OSC port is missing.");
    // With no owner remaining, diagnostic writes must let the child reach EOF
    // cleanup. The primary renderer's signal dispositions are untouched.
    if (::signal(SIGPIPE, SIG_IGN) == SIG_ERR)
        check(errno, "configure preview signal handling");
    ofSetDataPathRoot(root);
#else
    throw std::runtime_error("Separate preview windows are unavailable on this build.");
#endif
}

PreviewChild::~PreviewChild() {
#if VOSC_PREVIEW_PROCESS
    removeDirectory(directory, childDirectory);
    ::close(childDirectory);
    ::close(childChannel);
#endif
}

bool PreviewChild::poll() const {
#if VOSC_PREVIEW_PROCESS
    char commands[32];
    const auto count = ::recv(childChannel, commands, sizeof(commands), MSG_DONTWAIT);
    if (count == 0) return false;
    if (count < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
    if (std::string_view(commands, count).find('f') != std::string_view::npos) {
        if (auto* window = nativeWindow(); window && ::glfwGetWindowAttrib(window, GLFW_VISIBLE)) {
            if (::glfwGetWindowAttrib(window, GLFW_ICONIFIED)) ::glfwRestoreWindow(window);
            ::glfwFocusWindow(window);
        }
    }
    return true;
#else
    return false;
#endif
}

bool PreviewChild::announceReady(const std::string& marker) const {
#if VOSC_PREVIEW_PROCESS
    auto* window = nativeWindow();
    if (!window) return false;
    ::glfwSetWindowAttrib(window, GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    ::glfwShowWindow(window);

    const std::string wire = marker + '\n';
    size_t offset = 0;
    while (offset < wire.size()) {
        const auto count = ::send(childChannel, wire.data() + offset, wire.size() - offset, safeSendFlags);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += static_cast<size_t>(count);
    }
    return true;
#else
    (void)marker;
    return false;
#endif
}

} // namespace vosc::operator_ui
#endif
