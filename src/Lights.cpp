#include "Lights.h"
#include "Args.h"

ofLight& Lights::create(const ofxOscMessage& m) {
    string name = m.getArgType(0) == OFXOSC_TYPE_STRING ? m.getArgAsString(0) : ofToString(m.getArgAsInt(0));
    lights[name] = ofLight();
    ofLight& light = lights.at(name);
    // todo: to allow expressions ofLight needs to be wrapped inside Light class that extends VarsHolder
    light.setPosition(Args::parseVec3(m, 1));
    return light;
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
