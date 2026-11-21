// Real loopback UDP / oscpack / ofThreadChannel lifecycle regression checks.
#include "ofxOscReceiver.h"
#include "OscOutboundPacketStream.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;
namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
struct Hooks {
    std::mutex mutex;
    std::condition_variable changed;
    bool holdBeforeRun = false, beforeRun = false, releaseRun = false;
    bool stopRequested = false, finished = false;
    std::atomic<bool> failLaunch{false}, failWorker{false};
} hooks;

int freePort() {
    int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    require(fd >= 0, "port probe socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "port probe bind");
    socklen_t size = sizeof(address);
    require(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size) == 0, "port probe getsockname");
    int port = ntohs(address.sin_port);
    ::close(fd);
    return port;
}
void send(int port, int sequence) {
    char bytes[128];
    osc::OutboundPacketStream packet(bytes, sizeof(bytes));
    packet << osc::BeginMessage("/lifecycle") << sequence << osc::EndMessage;
    osc::UdpTransmitSocket socket(osc::IpEndpointName(127, 0, 0, 1, port));
    socket.Send(packet.Data(), packet.Size());
}
ofxOscReceiverSettings settings(int port) {
    ofxOscReceiverSettings value;
    value.port = port;
    value.reuse = false;
    return value;
}
void waitHook(bool Hooks::*member) {
    std::unique_lock<std::mutex> lock(hooks.mutex);
    require(hooks.changed.wait_for(lock, 2s, [&]{ return hooks.*member; }), "hook deadline");
}
struct ObservedReceiver : ofxOscReceiver {
    std::mutex mutex;
    std::condition_variable changed;
    int callbacks = 0;
    bool hold = false, entered = false, release = false, completed = false;
    ~ObservedReceiver() override { stop(); }
    void ProcessMessage(const osc::ReceivedMessage& message, const osc::IpEndpointName& remote) override {
        ofxOscReceiver::ProcessMessage(message, remote);
        std::unique_lock<std::mutex> lock(mutex);
        ++callbacks;
        entered = true;
        changed.notify_all();
        if (hold) changed.wait(lock, [&]{ return release; });
        completed = true;
        changed.notify_all();
    }
    void waitCount(int target) {
        std::unique_lock<std::mutex> lock(mutex);
        require(changed.wait_for(lock, 2s, [&]{ return callbacks >= target; }), "callback deadline");
    }
    void checkMessage(int sequence) {
        ofxOscMessage message;
        require(getNextMessage(message), "message queued");
        require(message.getAddress() == "/lifecycle", "message address");
        require(message.getArgAsInt32(0) == sequence, "fresh sequence");
        require(!getNextMessage(message), "no duplicate messages");
    }
};
void immediate() {
    for (int i = 0; i < 50; ++i) {
        ofxOscReceiver receiver;
        require(receiver.setup(settings(freePort())), "immediate setup");
        receiver.stop();
        receiver.stop();
        require(!receiver.isListening(), "stopped state");
    }
    // Prove the stop request survives a worker that has not entered Run.
    hooks.beforeRun = false;
    hooks.stopRequested = false;
    hooks.holdBeforeRun = true;
    ofxOscReceiver receiver;
    require(receiver.setup(settings(freePort())), "gated setup");
    waitHook(&Hooks::beforeRun);
    std::thread stopper([&]{ receiver.stop(); });
    waitHook(&Hooks::stopRequested);
    {
        std::lock_guard<std::mutex> lock(hooks.mutex);
        hooks.releaseRun = true;
    }
    hooks.changed.notify_all();
    stopper.join();
    require(!receiver.isListening(), "pre-Run stop completed");
    std::cout << "phase stop_requested -> run_released -> joined\n";
}
void destruction() {
    for (int i = 0; i < 50; ++i) {
        ofxOscReceiver receiver;
        require(receiver.setup(settings(freePort())), "destructor setup");
    }
}
void idle() {
    const int port = freePort();
    ObservedReceiver receiver;
    require(receiver.setup(settings(port)), "idle setup");
    send(port, 1);
    receiver.waitCount(1);
    receiver.checkMessage(1);
    auto start = std::chrono::steady_clock::now();
    receiver.stop();
    require(std::chrono::steady_clock::now() - start < 2s, "idle shutdown deadline");
    osc::UdpReceiveSocket rebound(osc::IpEndpointName(osc::IpEndpointName::ANY_ADDRESS, port), false);
    std::cout << "phase callback_ack -> idle_stop -> port_rebound\n";
}
void active() {
    const int port = freePort();
    ObservedReceiver receiver;
    receiver.hold = true;
    require(receiver.setup(settings(port)), "active setup");
    send(port, 2);
    receiver.waitCount(1);
    std::atomic<bool> stopReturned{false};
    std::thread stopper([&]{
        receiver.stop();
        std::lock_guard<std::mutex> lock(receiver.mutex);
        require(receiver.completed, "callback completes before stop returns");
        stopReturned.store(true);
    });
    waitHook(&Hooks::stopRequested);
    require(!stopReturned.load(), "stop waits for active callback");
    {
        std::lock_guard<std::mutex> lock(receiver.mutex);
        receiver.release = true;
    }
    receiver.changed.notify_all();
    stopper.join();
    require(stopReturned.load(), "stop returned after release");
    send(port, 3);
    require(receiver.callbacks == 1, "no callbacks after joined stop");
    receiver.checkMessage(2);
    std::cout << "phase callback_enter -> stop_requested -> callback_complete -> stop_return\n";
}
void restart() {
    const int port = freePort();
    ObservedReceiver receiver;
    auto config = settings(port);
    config.start = false;
    require(receiver.setup(config), "deferred setup");
    require(!receiver.isListening(), "not started");
    for (int i = 0; i < 12; ++i) {
        require(receiver.start(), "restart");
        require(receiver.start(), "idempotent start");
        send(port, i);
        receiver.waitCount(i + 1);
        receiver.stop();
        receiver.stop();
        receiver.checkMessage(i);
    }
}
void bindFailure() {
    const int port = freePort();
    ObservedReceiver receiver;
    {
        osc::UdpReceiveSocket occupied(osc::IpEndpointName(osc::IpEndpointName::ANY_ADDRESS, port), false);
        require(!receiver.setup(settings(port)), "bind conflict fails");
        require(!receiver.isListening(), "failed receiver not listening");
    }
    require(receiver.start(), "recover bind failure");
    send(port, 21);
    receiver.waitCount(1);
    receiver.stop();
    receiver.checkMessage(21);
}
void launchFailure() {
    const int port = freePort();
    ObservedReceiver receiver;
    hooks.failLaunch.store(true);
    require(!receiver.setup(settings(port)), "injected launch failure");
    require(!receiver.isListening(), "launch failure not listening");
    { osc::UdpReceiveSocket rebound(osc::IpEndpointName(osc::IpEndpointName::ANY_ADDRESS, port), false); }
    require(receiver.start(), "recover launch failure");
    send(port, 22);
    receiver.waitCount(1);
    receiver.stop();
    receiver.checkMessage(22);
}
void workerFailure() {
    const int port = freePort();
    ObservedReceiver receiver;
    hooks.failWorker.store(true);
    require(receiver.setup(settings(port)), "launch failing worker");
    waitHook(&Hooks::finished);
    require(!receiver.isListening(), "failed worker not listening");
    require(receiver.start(), "restart reaps failed worker without manual stop");
    send(port, 23);
    receiver.waitCount(1);
    receiver.stop();
    receiver.checkMessage(23);
}
} // namespace

void voscReceiverLifecycleHook(const char* phase) {
    const std::string name(phase);
    if (name == "before_launch" && hooks.failLaunch.exchange(false))
        throw std::runtime_error("injected worker-launch failure");
    if (name == "before_run" && hooks.failWorker.exchange(false))
        throw std::runtime_error("injected receive-loop failure");
    std::unique_lock<std::mutex> lock(hooks.mutex);
    if (name == "before_run") {
        hooks.beforeRun = true;
        hooks.changed.notify_all();
        if (hooks.holdBeforeRun) hooks.changed.wait(lock, []{ return hooks.releaseRun; });
    } else if (name == "stop_requested") hooks.stopRequested = true;
    else if (name == "worker_finished") hooks.finished = true;
    hooks.changed.notify_all();
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "one case required");
        const std::string name(argv[1]);
        if (name == "immediate") immediate();
        else if (name == "destruction") destruction();
        else if (name == "idle") idle();
        else if (name == "active") active();
        else if (name == "restart") restart();
        else if (name == "bind_failure") bindFailure();
        else if (name == "launch_failure") launchFailure();
        else if (name == "worker_failure") workerFailure();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << name << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
