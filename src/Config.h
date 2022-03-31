#pragma once

#include "ofMain.h"

#define MAX_SOUNDS 1
#define MAX_VISUALS 2
#define MAX_NOTES 10

enum Behaviour {
    B_RANDOM_SHADER = 1,
};

enum Layout {
    L_COLUMN = 0,
    L_ROW = 1,
    L_GRID = 2,
    L_STACK = 3,
};

class Config {
public:
    Config() {
        threshAmp = maxAmp / 2;
        threshLoud = maxLoud / 2;
    }
    Config(const Config &config) {
        maxAmp = config.maxAmp;
        maxLoud = config.maxLoud;
        speed = config.speed;
        behaviour = config.behaviour;
        color = config.color;
        threshAmp = config.threshAmp;
        threshLoud = config.threshLoud;
    }
    Config(float initValue) : maxAmp(initValue), maxLoud(initValue), speed(initValue) {
        
    }
    void merge(const Config &parent) {
        if (maxAmp == 0) maxAmp = parent.maxAmp;
        if (maxLoud == 0) maxLoud = parent.maxLoud;
        if (speed == 0) speed = parent.speed;
        if (behaviour < 0) behaviour = parent.behaviour;
        if (threshAmp < 0) threshAmp = parent.threshAmp;
        if (threshLoud < 0) threshLoud = parent.threshLoud;
    }
    bool randomShader() const {
        return behaviour > 0 && behaviour & B_RANDOM_SHADER;
    }
    
    float maxAmp = 0.7;
    float maxLoud = 64.0;
    float speed = 1.0;
    int behaviour = -1;
    ofFloatColor color = ofFloatColor(0);
    float threshAmp = -1;
    float threshLoud = -1;
};
