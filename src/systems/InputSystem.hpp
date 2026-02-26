#pragma once

#include "ofMain.h"
#include "ofxMidi.h"
#include "ofxOsc.h"
#include <functional>

class InputSystem {
public:
    void setup(unsigned int port);
    void forEachOscMessage(const std::function<void(const ofxOscMessage&)>& handler);
    void listMidiPorts();

private:
    ofxOscReceiver receiver;
    ofxMidiIn midiIn;
};
