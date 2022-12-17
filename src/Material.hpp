#pragma once

#include "VarsHolder.h"

class Material : public VarsHolder {
public:
    Material();
    void update();
    void oscCommand(const string& command, const ofxOscMessage& m);
    void begin() {}
    void end() {}
    
private:
    ofMaterialSettings settings;
    ofMaterial material;
};
