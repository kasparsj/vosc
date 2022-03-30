#include "Visual.h"

void Visual::update(vector<SoundData> &soundData, vector<TidalNote> &notes, Config &config) {
    bool isOnset = false;
    // todo: for tidal this does not work
    brightness = 0;
    for (int i = 0; i < notes.size(); i++) {
        if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
            //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
            float diff = ofGetElapsedTimef() - notes[i].timeStamp;
            if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                int instNum = notes[i].instNum;
                if (find(datas.begin(), datas.end(), "tidal" + ofToString(instNum)) != datas.end()) {
                    brightness += (int) (255 * notes[i].gain);
                    isOnset = true;
                }
            }
        }
    }
    for (int i=0; i<datas.size(); i++) {
        if (datas[i].substr(0, 3) == "amp") {
            int j = ofToInt(datas[i].substr(3));
            //brightness += (255 * tanh(soundData[j].amplitude / config.maxAmp * M_PI));
            brightness += (255 * int(soundData[j].amplitude > config.maxAmp / 2.f));
            isOnset = isOnset || (soundData[j].onset == 1);
        }
        else if (datas[i].substr(0, 4) == "loud") {
            int j = ofToInt(datas[i].substr(4));
            brightness += (255 * (soundData[j].loudness / config.maxLoud));
            isOnset = isOnset || (soundData[j].onset == 1);
        }
    }
    if (shader.isEnabled()) {
        shader.update(pos, size);
    }
    if (video.isEnabled()) {
        if (isOnset) {
            video.resetPos();
        }
        video.update();
    }
    if (brightness > 255) {
        brightness = 255;
    }
    if (behaviour & B_RANDOM_SHADER) {
        shader.random();
    }
}

void Visual::draw() {
    ofSetColor(brightness);
    if (shader.isEnabled()) {
        shader.draw(pos.x, pos.y, size.x, size.y);
    }
    if (video.isEnabled()) {
        video.draw(pos.x, pos.y, size.x, size.y);
    }
    brightness -= 32;
    if (brightness < 0) {
        brightness = 0;
    }
}
