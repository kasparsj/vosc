#include "Source.h"

void Source::update(const Config &config) {
    time = ofGetElapsedTimef() * config.speed;
}
