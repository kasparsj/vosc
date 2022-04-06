#include "LayerData.h"

void LayerData::update(const vector<string> dataSources, const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &globalConfig) {
    mergedConfig = Config(config);
    mergedConfig.merge(globalConfig);
    time = ofGetElapsedTimef() * mergedConfig.speed;
    tidal = dataSources.size() && dataSources[0].substr(0, 5) == "tidal";
    onset = false;
    values.clear();
    values.resize(dataSources.size());
    float thresh = 0;
    if (tidal) {
        for (int i = 0; i < notes.size(); i++) {
            if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
                //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
                float diff = ofGetElapsedTimef() - notes[i].timeStamp;
                if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                    int instNum = notes[i].instNum;
                    auto it = find(dataSources.begin(), dataSources.end(), "tidal" + ofToString(instNum));
                    if (it != dataSources.end()) {
                        int idx = it - dataSources.begin();
                        values[idx] += notes[i].gain;
                        onset = true;
                    }
                }
            }
        }
    }
    else {
        for (int i=0; i<dataSources.size(); i++) {
            if (i == 0) {
                thresh = mergedConfig.threshAmp;
            }
            if (dataSources[i].substr(0, 3) == "amp" || dataSources[i].substr(0, 4) == "loud") {
                int j;
                if (dataSources[i].substr(0, 3) == "amp") {
                    j = ofToInt(dataSources[i].substr(3));
                    //values[i] += tanh(sounds[j].amplitude / mergedConfig.maxAmp * M_PI);
                    values[i] += int(sounds[j].amplitude > mergedConfig.maxAmp / 2.f);
                }
                else {
                    j = ofToInt(dataSources[i].substr(4));
                    values[i] += (sounds[j].loudness / mergedConfig.maxLoud);
                    if (i == 0) {
                        thresh = mergedConfig.threshLoud;
                    }
                }
                onset = onset || (sounds[j].onset == 1);
                mfcc = sounds[j].mfcc;
            }
            else if (dataSources[i].substr(0, 5) == "const") {
                values[i] = (dataSources[i].size() > 5 ? std::stof(dataSources[i].substr(5)) : 1.0);
            }
            else if (dataSources[i].substr(0, 5) == "noise") {
                values[i] = ofNoise(i, time) * (dataSources[i].size() > 5 ? std::stof(dataSources[i].substr(5)) : 1.0);
            }
            else if (dataSources[i].substr(0, 4) == "rand") {
                values[i] = ofRandom(dataSources[i].size() > 4 ? std::stof(dataSources[i].substr(4)) : 1.0);
            }
        }
    }
    visible = onset || (!values.size() || values[0] > thresh);
    for (int i=0; i<values.size(); i++) {
        if (values[i] > 1.f) {
            values[i] = 1.f;
        }
    }
}

void LayerData::afterDraw() {
    if (tidal && values.size() >= 1) {
        values[0] -= 1.f/8.f;
        if (values[0] < 0) {
            values[0] = 0;
        }
    }
}