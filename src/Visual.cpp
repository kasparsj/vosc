#include "Visual.h"

void Visual::setup(int index, int numVisuals)
{
    this->index = index;
    this->total = numVisuals;
    datas.clear();
    datas.push_back("amp" + ofToString(index));
}

void Visual::layout(Layout layout)
{
    switch (layout) {
        case L_COLUMN:
            pos = glm::vec2(0, ofGetHeight() / total * index);
            size = glm::vec2(ofGetWidth(), ofGetHeight() / total);
            break;
        case L_ROW:
            pos = glm::vec2(ofGetWidth() / total * index, 0);
            size = glm::vec2(ofGetWidth() / total, ofGetHeight());
            break;
        case L_GRID: {
            int half = (int) ceil(total / 2.f);
            pos = glm::vec2(ofGetWidth() / half * (index % half), ofGetHeight() / 2 * floor(index / half));
            size = glm::vec2(ofGetWidth() / half, ofGetHeight() / 2);
            break;
        }
        case L_STACK:
            pos = glm::vec2(0, 0);
            size = glm::vec2(ofGetWidth(), ofGetHeight());
            break;
    }
}

void Visual::update(vector<SoundData> &soundData, vector<TidalNote> &notes) {
    isOnset = false;
    isTidal = datas[0].substr(0, 5) == "tidal";
    if (isTidal) {
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
    }
    else {
        brightness = 0;
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
    }
    if (shader.isEnabled()) {
        shader.update(index, pos, size, config);
    }
    if (video.isEnabled()) {
        if (isOnset) {
            video.resetPos();
        }
        video.update(config);
    }
    if (brightness > 255) {
        brightness = 255;
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
    if (isTidal) {
        brightness -= 32;
        if (brightness < 0) {
            brightness = 0;
        }
    }
}
