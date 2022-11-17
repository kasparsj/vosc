#include "Args.h"
#include "ColorUtil.h"

Args Args::instance;

ofFloatColor Args::parseColor(const ofxOscMessage &m, int idx) {
    ofFloatColor color;
    for (int i=idx; i<min(idx+3, (int) m.getNumArgs()); i++) {
        if (m.getArgType(i) == OFXOSC_TYPE_FLOAT) {
            color[i-idx] = m.getArgAsFloat(i);
        }
        else {
            color[i-idx] = m.getArgAsInt(i) / 255.f;
        }
    }
    return color;
}

void Args::updateFloats() {
    vector<float*> toDelete;
    for (auto const& [key, tween] : floatTweens) {
        float endTime = tween.start + tween.dur;
        if (*key == tween.to || ofGetElapsedTimef() >= endTime) {
            *key = tween.to;
            toDelete.push_back(key);
        }
        else {
            *key = ofxeasing::map(ofGetElapsedTimef(), tween.start, endTime, tween.from, tween.to, tween.ease);
        }
    }
    for (int i=0; i<toDelete.size(); i++) {
        floatTweens.erase(toDelete[i]);
    }
}

void Args::updateVecs() {
    vector<glm::vec3*> toDelete;
    for (auto const& [key, tween] : vec3Tweens) {
        float endTime = tween.start + tween.dur;
        if (*key == tween.to || ofGetElapsedTimef() >= endTime) {
            *key = tween.to;
            toDelete.push_back(key);
        }
        else {
            glm::vec3 value;
            value.x = ofxeasing::map(ofGetElapsedTimef(), tween.start, endTime, tween.from.x, tween.to.x, tween.ease);
            value.y = ofxeasing::map(ofGetElapsedTimef(), tween.start, endTime, tween.from.y, tween.to.y, tween.ease);
            value.z = ofxeasing::map(ofGetElapsedTimef(), tween.start, endTime, tween.from.z, tween.to.z, tween.ease);
            *key = value;
        }
    }
    for (int i=0; i<toDelete.size(); i++) {
        vec3Tweens.erase(toDelete[i]);
    }
}

void Args::updateColors() {
    vector<ofFloatColor*> toDelete;
    for (auto const& [key, tween] : colorTweens) {
        float endTime = tween.start + tween.dur;
        if (*key == tween.to || ofGetElapsedTimef() >= endTime) {
            *key = tween.to;
            toDelete.push_back(key);
        }
        else {
            float perc = (ofGetElapsedTimef() - tween.start) / tween.dur;
            *key = ofxColorTheory::ColorUtil::lerpLch(tween.from, tween.to, perc);
        }
    }
    for (int i=0; i<toDelete.size(); i++) {
        colorTweens.erase(toDelete[i]);
    }
}

void Args::handleFloat(float *value, const ofxOscMessage &m, int i) {
    if (m.getNumArgs() > (i+1)) {
        if (m.getNumArgs() > (i+2)) {
            createTween(value, m.getArgAsFloat(i), m.getArgAsFloat(i+1), m.getArgAsString(i+2));
        }
        else {
            createTween(value, m.getArgAsFloat(i), m.getArgAsFloat(i+1));
        }
    }
    else {
        *value = m.getArgAsFloat(i);
    }
}

void Args::handlePercent(float *value, const ofxOscMessage &m, int i) {
    if (m.getNumArgs() > 2) {
        if (m.getNumArgs() > 3) {
            createTween(value,
                        m.getArgType(1) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(1) > 1.f ? m.getArgAsFloat(1) / 100.f : m.getArgAsFloat(1),
                        m.getArgType(2) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(2) > 1.f ? m.getArgAsFloat(2) / 100.f : m.getArgAsFloat(2),
                        m.getArgAsString(3));
        }
        else {
            createTween(value,
                        m.getArgType(1) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(1) > 1.f ? m.getArgAsFloat(1) / 100.f : m.getArgAsFloat(1),
                        m.getArgType(2) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(2) > 1.f ? m.getArgAsFloat(2) / 100.f : m.getArgAsFloat(2));
        }
    }
    else {
        *value = m.getArgType(1) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(1) > 1.f ? m.getArgAsFloat(1) / 100.f : m.getArgAsFloat(1);
    }
}

void Args::handleVec3(glm::vec3 *value, const ofxOscMessage &m, int firstArg) {
    if (m.getNumArgs() > firstArg + 3) {
        createTween(value, glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg+1), m.getArgAsFloat(firstArg+2)), m.getArgAsFloat(firstArg+3));
    }
    else if (m.getNumArgs() > firstArg+1) {
        *value = glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg+1), m.getNumArgs() > firstArg+2 ? m.getArgAsFloat(firstArg+2) : 0);
    }
    else {
        *value = glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg));
    }
}

void Args::handleColor(ofFloatColor *value, const ofxOscMessage &m) {
    if (m.getNumArgs() > 4) {
        createTween(value, parseColor(m, 1), m.getArgAsFloat(4));
    }
    else {
        *value = parseColor(m, 1);
    }
}

void Args::createTween(float *value, float target, float dur, ofxeasing::function ease) {
    Tween<float> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    floatTweens[value] = tween;
}

void Args::createTween(glm::vec3 *value, const glm::vec3 &target, float dur, ofxeasing::function ease) {
    Tween<glm::vec3> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    vec3Tweens[value] = tween;
}

void Args::createTween(ofFloatColor *value, const ofFloatColor &target, float dur, ofxeasing::function ease) {
    Tween<ofFloatColor> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    colorTweens[value] = tween;
}

