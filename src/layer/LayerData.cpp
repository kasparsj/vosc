#include "LayerData.h"
#include "Layer.h"

void LayerData::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    const float timef = ofGetElapsedTimef();
    time += ((timef - prevTime) * layer->speed);
    prevTime = timef;
    const vector<string> &ds = layer->dataSources;
    tidal = ds.size() && ds[0].substr(0, 5) == "tidal";
    if (!tidal) {
        reset();
    }
    values.resize(ds.size());
    if (tidal) {
        for (int i = 0; i < notes.size(); i++) {
            if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
                //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
                float diff = ofGetElapsedTimef() - notes[i].timeStamp;
                if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                    int instNum = notes[i].instNum;
                    auto it = find(ds.begin(), ds.end(), "tidal" + ofToString(instNum));
                    if (it != ds.end()) {
                        int idx = it - ds.begin();
                        values[idx] += notes[i].gain;
                        onset = true;
                    }
                }
            }
        }
    }
    else {
        for (int i=0; i<ds.size(); i++) {
            if (ds[i].substr(0, 3) == "amp" || ds[i].substr(0, 4) == "loud") {
                int j;
                if (ds[i].substr(0, 3) == "amp") {
                    j = ofToInt(ds[i].substr(3));
                    values[i] += (sounds[j].amplitude / sounds[j].maxAmp);
                }
                else {
                    j = ofToInt(ds[i].substr(4));
                    values[i] += (sounds[j].loudness / sounds[j].maxLoud);
                }
                onset = onset || (sounds[j].onset == 1);
                mfcc = sounds[j].mfcc;
            }
            else {
                if (ds[i].substr(0, 5) == "const") {
                    values[i] = (ds[i].size() > 5 ? std::stof(ds[i].substr(5)) : 1.0);
                }
                else if (ds[i].substr(0, 5) == "noise") {
                    values[i] = ofNoise(i, time) * (ds[i].size() > 5 ? std::stof(ds[i].substr(5)) : 1.0);
                }
                else if (ds[i].substr(0, 4) == "rand") {
                    values[i] = ofRandom(ds[i].size() > 4 ? std::stof(ds[i].substr(4)) : 1.0);
                }
                else if (ds[i].substr(0, 3) == "sin") {
                    values[i] = abs(sin(time));
                }
                if (values.size() > 0 && i == 0) {
                    onset = values[0] >= layer->onsetThresh;
                    // trigger onset once - only when crossing thresh going up
                    if (onset && prevOnset) {
                        onset = false;
                        prevOnset = true;
                    }
                    else {
                        prevOnset = onset;
                    }
                }
            }
        }
    }
    if (values.size() > 0 && values[0] > 1.f) {
        values[0] = 1.f;
    }
    visible = onset || (values.size() == 0 || values[0] >= layer->thresh);
}

void LayerData::afterDraw() {
    if (tidal && values.size() >= 1) {
        values[0] -= 1.f/8.f;
        if (values[0] < 0) {
            values[0] = 0;
        }
    }
}
