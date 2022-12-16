#pragma once

#include "Light.hpp"

class Lights {
public:
    static Lights& get() {
        return instance;
    }
    
    const map<string, shared_ptr<Light>>& all() {
        return lights;
    }
    shared_ptr<Light>& create(const ofxOscMessage& m);
    void remove(const ofxOscMessage& m);
    
private:
    static Lights instance;
    
    map<string, shared_ptr<Light>> lights;
};
