#include "Sound.h"

void Sound::parse(const ofxOscMessage &m) {
    instNum = m.getArgAsInt(0);
    amplitude = m.getArgAsFloat(1);
    loudness = m.getArgAsFloat(2);
    onset = m.getArgAsInt(3);
    mfcc = ofFloatColor(m.getArgAsFloat(4), m.getArgAsFloat(5), m.getArgAsFloat(6));
}
