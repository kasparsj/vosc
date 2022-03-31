#include "ShaderData.h"

void ShaderData::update(const vector<string> dataSource, const vector<Sound> &sounds, const vector<TidalNote> &notes, const Config &globalConfig) {
    mergedConfig = Config(config);
    mergedConfig.merge(globalConfig);
    tidal = dataSource[0].substr(0, 5) == "tidal";
    onset = false;
    float thresh = 0;
    if (tidal) {
        for (int i = 0; i < notes.size(); i++) {
            if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
                //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
                float diff = ofGetElapsedTimef() - notes[i].timeStamp;
                if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                    int instNum = notes[i].instNum;
                    auto it = find(dataSource.begin(), dataSource.end(), "tidal" + ofToString(instNum));
                    if (it != dataSource.end()) {
                        int idx = it - dataSource.begin();
                        values[idx] += notes[i].gain;
                        onset = true;
                    }
                }
            }
        }
    }
    else {
        values[0] = 0;
        for (int i=0; i<dataSource.size(); i++) {
            if (dataSource[i].substr(0, 3) == "amp") {
                int j = ofToInt(dataSource[i].substr(3));
                //values[i] += tanh(sounds[j].amplitude / mergedConfig.maxAmp * M_PI);
                values[i] += int(sounds[j].amplitude > mergedConfig.maxAmp / 2.f);
                onset = onset || (sounds[j].onset == 1);
                if (i == 0) {
                    thresh = mergedConfig.threshAmp;
                }
            }
            else if (dataSource[i].substr(0, 4) == "loud") {
                int j = ofToInt(dataSource[i].substr(4));
                values[i] += (sounds[j].loudness / mergedConfig.maxLoud);
                onset = onset || (sounds[j].onset == 1);
                if (i == 0) {
                    thresh = mergedConfig.threshLoud;
                }
            }
            else if (dataSource[i].substr(0, 6) == "static") {
                values[i] = 0.5f;
            }
        }
    }
    visible = onset || values[0] > thresh;
    for (int i=0; i<MAX_VISUALS; i++) {
        if (values[i] > 1.f) {
            values[i] = 1.f;
        }
    }
}

void ShaderData::afterDraw() {
    if (tidal) {
        values[0] -= 1.f/8.f;
        if (values[0] < 0) {
            values[0] = 0;
        }
    }
}
