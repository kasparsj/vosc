#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include <functional>
#include <string_view>

bool validDraftSlot(std::string_view slot);

class InputSystem {
public:
    bool setup(unsigned int port);
    void shutdown();
    void forEachOscMessage(const std::function<void(const ofxOscMessage&)>& handler);
    void listMidiPorts();

private:
    ofxOscReceiver receiver;
};
