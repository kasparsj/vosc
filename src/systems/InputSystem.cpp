#include "InputSystem.hpp"
#include "ofxMidi.h"
#include <exception>

void InputSystem::setup(unsigned int port) {
    receiver.setup(port);
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
