#pragma once

#include "Camera.hpp"
#include "input/Inputs.hpp"
#include "ofxTidalCycles.h"
#include <memory>

struct RuntimeContext {
    RuntimeContext()
        : camera()
        , inputs(Inputs::get())
        , tidal(std::unique_ptr<ofxTidalCycles>(new ofxTidalCycles(1))) {}

    Camera camera;
    Inputs& inputs;
    std::unique_ptr<ofxTidalCycles> tidal;
};
