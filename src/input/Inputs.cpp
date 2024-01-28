#include "Inputs.hpp"

Inputs Inputs::instance;

void Inputs::oscCommand(string command, const ofxOscMessage &m) {
    if (command == "/input/list") {
        Inputs::get().log();
    }
    else {
        string name = m.getArgType(0) == OFXOSC_TYPE_STRING ? m.getArgAsString(0) : ofToString(m.getArgAsInt(0));
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
    string name = m.getArgType(0) == OFXOSC_TYPE_STRING ? m.getArgAsString(0) : ofToString(m.getArgAsInt(0));
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
