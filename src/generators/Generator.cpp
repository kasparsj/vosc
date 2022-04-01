#include "Generator.h"

void Generator::update(const Config &config) {
    time = ofGetElapsedTimef() * config.speed;
}
