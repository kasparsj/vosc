#include "Mic.h"

void Mic::setupStream(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    int numInputChannels = m.getNumArgs() > 2 ? m.getArgAsInt(2) : 1;
    if (numInputChannels > 0) {
        //instNum = m.getArgAsInt(0);
        auto devices = soundStream.getMatchingDevices(name);
        ofSoundStreamSettings settings;
        if (!devices.empty()) {
            settings.setInDevice(devices[0]);
        }

        settings.setInListener(this);
        settings.sampleRate = 44100;
        settings.numOutputChannels = 0;
        settings.numInputChannels = numInputChannels;
        settings.bufferSize = m.getNumArgs() > 3 ? m.getArgAsInt(3) : 256;
        soundStream.setup(settings);
        soundStream.start();
    }
    else {
        soundStream.close();
    }
}

void Mic::audioIn(ofSoundBuffer& input){
    float curVol = 0.0;
    int numCounted = 0;
    int numChan = soundStream.getNumInputChannels();
    for (size_t i = 0; i < input.getNumFrames(); i++){
        for (size_t j=0; j<soundStream.getNumInputChannels(); j++) {
            float sample = input[i * numChan + j] / numChan;
            curVol += sample * sample;
            numCounted++;
        }
    }
    curVol /= (float) numCounted;
    amplitude = sqrt( curVol );
    
    ampSmooth *= 0.93;
    ampSmooth += 0.07 * amplitude;
    ampScaled = ofMap(ampSmooth, 0.0, maxAmp, 0.0, 1.0, true);
}
