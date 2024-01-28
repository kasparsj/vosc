#include "OSCInput.h"
#include "Args.h"

void OSCInput::set(const ofxOscMessage& m, int idx) {
    //instNum = m.getArgAsInt(0);
    amplitude = m.getArgAsFloat(idx);
    ampScaled = ofMap(amplitude, 0.0, getVar("maxAmp"), 0.0, 1.0, true);
    loudness = m.getArgAsFloat(idx+1);
    onset = m.getArgAsInt(idx+2);
    mfcc.clear();
    if (m.getNumArgs()-(idx+3) > 0) {
        mfcc.resize(m.getNumArgs()-(idx+3));
        for (int i=0; i<m.getNumArgs()-(idx+3); i++) {
            mfcc[i] = m.getArgAsFloat(idx+3+i);
        }
    }
}

void OSCInput::oscCommand(const string& command, const ofxOscMessage &m) {
    if (command == "/input") {
        setupStream(m);
    }
    else if (command == "/input/data") {
        set(m, 1);
    }
    else if (command == "/input/set") {
        string prop = m.getArgAsString(1);
        if (prop == "maxAmp") {
            setVar("maxAmp", m, 2);
        }
        else if (prop == "maxLoud") {
            setVar("maxLoud", m, 2);
        }
    }
}
