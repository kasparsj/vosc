#include "InputSystem.hpp"
#include "ofxMidi.h"
#include <exception>

bool InputSystem::setup(unsigned int port, bool preview) {
    if (!preview) return receiver.setup(port);
    ofxOscReceiverSettings settings;
    settings.port = port;
    settings.host = "127.0.0.1";
    settings.reuse = false;
    return receiver.setup(settings);
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
