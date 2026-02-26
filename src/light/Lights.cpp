#include "Lights.h"
#include "Args.h"

Lights Lights::instance;

namespace {
bool tryGetLightName(const ofxOscMessage& m, string& name) {
    if (m.getNumArgs() < 1) {
        ofLogError() << "light command missing target argument: " << m;
        return false;
    }
    const auto type = m.getArgType(0);
    if (type == OFXOSC_TYPE_STRING) {
        name = m.getArgAsString(0);
        return true;
    }
    if (type == OFXOSC_TYPE_INT32 || type == OFXOSC_TYPE_INT64) {
        name = ofToString(m.getArgAsInt(0));
        return true;
    }
    ofLogError() << "light command target must be string or int: " << m;
    return false;
}
}  // namespace

shared_ptr<Light>& Lights::create(const ofxOscMessage& m) {
    string name;
    if (!tryGetLightName(m, name)) {
        static shared_ptr<Light> nullLight = make_shared<Light>();
        return nullLight;
    }
    lights[name] = make_shared<Light>();
    shared_ptr<Light>& light = lights.at(name);
    light->set(m, 1);
    return light;
}

void Lights::remove(const ofxOscMessage& m) {
    string name;
    if (!tryGetLightName(m, name)) {
        return;
    }
    lights.erase(name);
}

void Lights::update() {
    for (map<string, shared_ptr<Light>>::iterator it=lights.begin(); it!=lights.end(); ++it) {
        it->second->update();
    }
}
