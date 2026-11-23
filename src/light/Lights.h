#pragma once

#include "Light.hpp"

class Lights {
public:
    static Lights& get();
    
    const map<string, shared_ptr<Light>>& all() {
        return lights;
    }
    shared_ptr<Light>& create(const ofxOscMessage& m);
    void remove(const ofxOscMessage& m);
    void clear() { lights.clear(); }
    void update();
    
private:
    map<string, shared_ptr<Light>> lights;
};
