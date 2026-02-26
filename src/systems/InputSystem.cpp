#include "InputSystem.hpp"

void InputSystem::setup(unsigned int port) {
    receiver.setup(port);
}

void InputSystem::forEachOscMessage(const std::function<void(const ofxOscMessage&)>& handler) {
    while (receiver.hasWaitingMessages()) {
        ofxOscMessage message;
        receiver.getNextMessage(message);
        handler(message);
    }
}

void InputSystem::listMidiPorts() {
    vector<string> inPorts = midiIn.getInPortList();
    ofLog() << "MIDI in ports:";
    for (int i = 0; i < inPorts.size(); i++) {
        ofLog() << (ofToString(i) + ": " + inPorts[i]);
    }
}
