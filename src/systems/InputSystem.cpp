#include "InputSystem.hpp"
#include "ofxMidi.h"
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <exception>
#if defined(__unix__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

bool validDraftSlot(std::string_view slot) {
    const auto letter = [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z'); };
    if (slot.empty() || slot.size() > 32 || !letter(slot.front())) return false;
    for (char ch : slot)
        if (!letter(ch) && !(ch >= '0' && ch <= '9') && ch != '_') return false;
    return true;
}

bool InputSystem::setup(unsigned int port) {
    return receiver.setup(port);
}

std::optional<unsigned int> InputSystem::setupDraftSamples() {
#if defined(__unix__) || defined(__APPLE__)
    const int candidate = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (candidate < 0) return std::nullopt;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t size = sizeof(address);
    if (::fcntl(candidate, F_SETFD, FD_CLOEXEC) < 0 ||
        ::bind(candidate, reinterpret_cast<sockaddr*>(&address), size) < 0 ||
        ::getsockname(candidate, reinterpret_cast<sockaddr*>(&address), &size) < 0) {
        ::close(candidate);
        return std::nullopt;
    }
    draftSocket = candidate;
    return ntohs(address.sin_port);
#else
    return std::nullopt;
#endif
}

void InputSystem::shutdown() {
    receiver.stop();
#if defined(__unix__) || defined(__APPLE__)
    if (draftSocket >= 0) ::close(draftSocket);
#endif
    draftSocket = -1;
}

void InputSystem::forEachOscMessage(const std::function<void(const ofxOscMessage&)>& handler) {
    while (receiver.hasWaitingMessages()) {
        ofxOscMessage message;
        receiver.getNextMessage(message);
        handler(message);
    }
#if defined(__unix__) || defined(__APPLE__)
    if (draftSocket < 0) return;
    constexpr std::string_view header("/input/data\0,sf\0", 16);
    std::array<char, 128> packet{};
    for (int count = 0; count < 64; ++count) {
        const auto received = ::recv(draftSocket, packet.data(), packet.size(), MSG_DONTWAIT);
        if (received < 0) break;
        const auto size = static_cast<size_t>(received);
        const std::string_view bytes(packet.data(), size);
        if (size < header.size() + 8 || !bytes.starts_with(header)) continue;
        const size_t end = bytes.find('\0', header.size());
        if (end == std::string_view::npos) continue;
        const auto slot = bytes.substr(header.size(), end - header.size());
        const size_t data = (end + 4) & ~size_t(3);
        if (!validDraftSlot(slot) || data + 4 != size ||
            bytes.substr(end, data - end).find_first_not_of('\0') != std::string_view::npos) continue;
        uint32_t raw;
        std::memcpy(&raw, packet.data() + data, sizeof(raw));
        const float value = std::bit_cast<float>(ntohl(raw));
        if (!std::isfinite(value) || value < 0.f || value > 1.f) continue;
        ofxOscMessage sample;
        sample.setAddress("/input/data");
        sample.addStringArg(std::string(slot));
        sample.addFloatArg(value);
        handler(sample); // Existing OSC parser supplies omitted loudness and onset.
    }
#endif
}

void InputSystem::listMidiPorts() {
    try {
        ofxMidiIn midiIn;
        const auto inPorts = midiIn.getInPortList();
        ofLog() << "MIDI in ports:";
        for (std::size_t i = 0; i < inPorts.size(); i++) {
            ofLog() << (ofToString(i) + ": " + inPorts[i]);
        }
    } catch (const std::exception& error) {
        ofLogWarning("MIDI") << "Could not list input ports: " << error.what();
    }
}
