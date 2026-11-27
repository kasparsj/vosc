#include "InputSystem.hpp"
#include "ofxMidi.h"
#include <exception>

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

void InputSystem::shutdown() {
    receiver.stop();
}

void InputSystem::forEachOscMessage(const std::function<void(const ofxOscMessage&)>& handler) {
    while (receiver.hasWaitingMessages()) {
        ofxOscMessage message;
        receiver.getNextMessage(message);
        handler(message);
    }
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
