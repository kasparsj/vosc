#pragma once

#include "OSCInput.h"

class Inputs {
public:
    static Inputs& get() {
        return instance;
    }
    
    const map<string, shared_ptr<OSCInput>>& all() {
        return inputs;
    }
    void oscCommand(string command, const ofxOscMessage &m);
    void remove(const ofxOscMessage& m);
    void update();
    bool checkOnset();
    void log();
    
private:
    static Inputs instance;
    
    map<string, shared_ptr<OSCInput>> inputs;
};
