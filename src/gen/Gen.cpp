#include "Gen.h"

void Gen::update(const Config &config) {
    time = ofGetElapsedTimef() * config.speed;
}
