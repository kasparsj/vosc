#include "Visual.h"

void Visual::update(vector<SoundData> &soundData, vector<TidalNote> &notes, Config &config) {
    // todo: just testing
    brightness = 0;
    for (int i = 0; i < notes.size(); i++) {
        if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
            //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
            float diff = ofGetElapsedTimef() - notes[i].timeStamp;
            if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                int instNum = notes[i].instNum;
                if (find(sources.begin(), sources.end(), "tidal" + ofToString(instNum)) != sources.end()) {
                    brightness += (int) (255 * notes[i].gain);
                }
            }
        }
    }
    for (int i=0; i<sources.size(); i++) {
        if (sources[i].substr(0, 3) == "amp") {
            int j = ofToInt(sources[i].substr(3));
            //brightness += (255 * tanh(soundData[j].amplitude / config.maxAmp * M_PI));
            brightness += (255 * int(soundData[j].amplitude > config.maxAmp / 2.f));
        }
        else if (sources[i].substr(0, 4) == "loud") {
            int j = ofToInt(sources[i].substr(4));
            brightness += (255 * (soundData[j].loudness / config.maxLoud));
        }
    }
    shader.update(pos, size);
    if (brightness > 255) {
        brightness = 255;
    }
    if (behaviour & B_RANDOM_SHADER) {
        shader.random();
    }
}

void Visual::draw() {
    ofSetColor(brightness);
    shader.draw(pos.x, pos.y, size.x, size.y);
    brightness -= 32;
    if (brightness < 0) {
        brightness = 0;
    }
}
