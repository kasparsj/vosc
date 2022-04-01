#pragma once

#include "Gen.h"

class FBOGen : public Gen {
public:
    virtual void clear();
    
    ofFbo fbo;
};
