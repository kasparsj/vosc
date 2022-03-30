#include "SoundData.h"

void SoundData::parse(ofxOscMessage &m) {
    instNum = m.getArgAsInt(0);
    amplitude = m.getArgAsFloat(1);
    loudness = m.getArgAsFloat(2);
    onset = m.getArgAsInt(3);
}
