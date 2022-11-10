#include "Lights.h"

void Lights::create(const ofxOscMessage& m) {
    string name = m.getArgType(0) == OFXOSC_TYPE_STRING ? m.getArgAsString(0) : ofToString(m.getArgAsInt(0));
    lights[name] = ofLight();
    if (m.getNumArgs() >= 4) {
        lights[name].setPosition(m.getArgAsInt(1), m.getArgAsInt(2), m.getArgAsInt(3));
    }
}

void Lights::remove(const ofxOscMessage& m) {
    string name = m.getArgType(0) == OFXOSC_TYPE_STRING ? m.getArgAsString(0) : ofToString(m.getArgAsInt(0));
    lights.erase(name);
}

void Lights::enable() {
    for (map<string, ofLight>::iterator it=lights.begin(); it!=lights.end(); ++it) {
        it->second.enable();
    }
}

void Lights::disable() {
    for (map<string, ofLight>::iterator it=lights.begin(); it!=lights.end(); ++it) {
        it->second.disable();
    }
}
