#pragma once

#include "OSCInput.h"

class Inputs {
public:
    static Inputs& get();
    
    const map<string, shared_ptr<OSCInput>>& all() {
        return inputs;
    }
    const map<string, shared_ptr<OSCInput>>& all() const {
        return inputs;
    }
    bool hasConfiguredTarget(const ofxOscMessage& m) const;
    void oscCommand(string command, const ofxOscMessage &m);
    void remove(const ofxOscMessage& m);
    void update();
    bool checkOnset();
    void log();
    void clear();
    void swap(Inputs& other) noexcept { inputs.swap(other.inputs); }
    
private:
    map<string, shared_ptr<OSCInput>> inputs;
};
