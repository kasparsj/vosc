#include "Sound.h"

void Sound::parse(const ofxOscMessage& m) {
    //instNum = m.getArgAsInt(0);
    amplitude = m.getArgAsFloat(1);
    ampScaled = ofMap(amplitude, 0.0, maxAmp, 0.0, 1.0, true);
    loudness = m.getArgAsFloat(2);
    onset = m.getArgAsInt(3);
    mfcc.clear();
    if (m.getNumArgs()-4 > 0) {
        mfcc.resize(m.getNumArgs()-4);
        for (int i=0; i<m.getNumArgs()-4; i++) {
            mfcc[i] = m.getArgAsFloat(4+i);
        }
    }
}
