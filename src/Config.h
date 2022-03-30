#pragma once

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
    float maxAmp = 0.7;
    float maxLoud = 64.0;
    float speed = 1.0;
    int behaviour = 0;
};
