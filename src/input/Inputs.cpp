#include "Inputs.hpp"

Inputs Inputs::instance;

namespace {
bool tryGetInputName(const ofxOscMessage& m, string& name) {
    if (m.getNumArgs() < 1) {
        ofLogError() << "input command missing target argument: " << m;
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
    ofLogError() << "input command target must be string or int: " << m;
    return false;
}
}  // namespace

void Inputs::oscCommand(string command, const ofxOscMessage &m) {
    if (command == "/input/list") {
        log();
    }
    else {
        string name;
        if (!tryGetInputName(m, name)) {
            return;
        }
        if (name == "*") {
            for (map<string, shared_ptr<OSCInput>>::iterator it=inputs.begin(); it!=inputs.end(); ++it) {
                it->second->oscCommand(command, m);
            }
        }
        else {
            if (inputs.find(name) == inputs.end()) {
                inputs[name] = make_shared<OSCInput>();
            }
            shared_ptr<OSCInput>& input = inputs.at(name);
            input->oscCommand(command, m);
        }
    }
}

void Inputs::remove(const ofxOscMessage& m) {
    string name;
    if (!tryGetInputName(m, name)) {
        return;
    }
    inputs.erase(name);
}

void Inputs::update() {
    for (map<string, shared_ptr<OSCInput>>::iterator it=inputs.begin(); it!=inputs.end(); ++it) {
        it->second->update();
    }
}

bool Inputs::checkOnset() {
    for (map<string, shared_ptr<OSCInput>>::iterator it=inputs.begin(); it!=inputs.end(); ++it) {
        if (it->second->onset) {
            return true;
        }
    }
    return false;
}

void Inputs::log() {
    vector<ofSoundDevice> devices = ofSoundStreamListDevices();
    ofLog() << ("ofSoundStreamListDevices:");
    for (int i=0; i<devices.size(); i++) {
        ofLog() << (ofToString(i) + ": " + devices[i].name);
    }
}
