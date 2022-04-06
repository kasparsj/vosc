#pragma once

#include "Gen.h"

class FBOGen : public Gen {
public:
    FBOGen(string name) : Gen(name) {}
    void clear() override;
    
    ofFbo fbo;
};
