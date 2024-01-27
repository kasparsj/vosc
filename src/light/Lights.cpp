#include "Lights.h"
#include "Args.h"

Lights Lights::instance;

shared_ptr<Light>& Lights::create(const ofxOscMessage& m) {
    string name = m.getArgType(0) == OFXOSC_TYPE_STRING ? m.getArgAsString(0) : ofToString(m.getArgAsInt(0));
    lights[name] = make_shared<Light>();
    shared_ptr<Light>& light = lights.at(name);
    light->set(m, 1);
    return light;
}

void Lights::remove(const ofxOscMessage& m) {
    string name = m.getArgType(0) == OFXOSC_TYPE_STRING ? m.getArgAsString(0) : ofToString(m.getArgAsInt(0));
    lights.erase(name);
}

void Lights::update() {
    for (map<string, shared_ptr<Light>>::iterator it=lights.begin(); it!=lights.end(); ++it) {
        it->second->update();
    }
}
