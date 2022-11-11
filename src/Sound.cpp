#include "Sound.h"

void Sound::update() {
    volumeScaled = ofMap(volumeSmooth, 0.0, maxVol, 0.0, 1.0, true);
    volumeHist.push_back( volumeScaled );
    if( volumeHist.size() >= 400 ){
        volumeHist.erase(volumeHist.begin(), volumeHist.begin()+1);
    }
}

void Sound::parse(const ofxOscMessage& m) {
    //instNum = m.getArgAsInt(0);
    amplitude = m.getArgAsFloat(1);
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

void Sound::stream(const ofxOscMessage& m) {
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

void Sound::audioIn(ofSoundBuffer& input){
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
    volume = sqrt( curVol );
    
    volumeSmooth *= 0.93;
    volumeSmooth += 0.07 * volume;
}
