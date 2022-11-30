#include "Args.h"
#include "ColorUtil.h"

Args Args::instance;

float Args::parseIntOrFloat(const ofxOscMessage &m, int i) {
    if (m.getArgType(i) == OFXOSC_TYPE_FLOAT) {
        return m.getArgAsFloat(i);
    }
    else {
        return m.getArgAsInt(i) / 255.f;
    }
}

ofFloatColor Args::parseColor(const ofxOscMessage &m, int idx) {
    ofFloatColor color;
    for (int i=idx; i<min(idx+3, (int) m.getNumArgs()); i++) {
        color[i-idx] = parseIntOrFloat(m, idx);
    }
    return color;
}

ofFloatColor Args::parseLerpColor(const ofxOscMessage &m, int firstArg) {
    ofFloatColor fromColor = Args::getInstance().parseColor(m, firstArg+2);
    ofFloatColor toColor = Args::getInstance().parseColor(m, firstArg+5);
    float perc = m.getArgAsFloat(firstArg+1);
    return ofxColorTheory::ColorUtil::lerpLch(fromColor, toColor, perc);
}

vector<float> Args::parseAlign(const ofxOscMessage &m, int firstArg) {
    string argH = m.getArgAsString(firstArg);
    string argV = m.getNumArgs() > 2 ? m.getArgAsString(firstArg+1) : argH;
    vector<float> align = {0, 0, 0};
    if (argH == "center") {
        align[0] = 0.5f;
    }
    else if (argH == "left") {
        align[0] = 0.f;
    }
    else if (argH == "right") {
        align[0] = 1.f;
    }
    if (argV == "center") {
        align[1] = 0.5f;
    }
    else if (argV == "top") {
        align[1] = 0.f;
    }
    else if (argV == "bottom") {
        align[1] = 1.f;
    }
    return align;
}

void Args::updateFloats() {
    vector<float*> toDelete;
    for (auto const& [key, tween] : floatTweens) {
        float endTime = tween.start + tween.dur;
        if (*key == tween.to || ofGetElapsedTimef() >= endTime) {
            *key = tween.to;
            toDelete.push_back(key);
            tween.onComplete();
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
            tween.onComplete();
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
            tween.onComplete();
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

void Args::handleFloat(float* value, const ofxOscMessage &m, int i) {
    if (m.getNumArgs() > (i+1)) {
        if (m.getNumArgs() > (i+2)) {
            createTween(value, m.getArgAsFloat(i), m.getArgAsFloat(i+1), function<void()>(), m.getArgAsString(i+2));
        }
        else {
            createTween(value, m.getArgAsFloat(i), m.getArgAsFloat(i+1));
        }
    }
    else {
        setFloat(value, m, i);
    }
}

void Args::setFloat(float* value, const ofxOscMessage& m, int i) {
    *value = m.getArgAsFloat(i);
}

void Args::handlePercent(float *value, const ofxOscMessage &m, int i) {
    if (m.getNumArgs() > 2) {
        if (m.getNumArgs() > 3) {
            createTween(value,
                        m.getArgType(1) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(1) > 1.f ? m.getArgAsFloat(1) / 100.f : m.getArgAsFloat(1),
                        m.getArgType(2) == OFXOSC_TYPE_INT32 || m.getArgAsFloat(2) > 1.f ? m.getArgAsFloat(2) / 100.f : m.getArgAsFloat(2),
                        function<void()>(), m.getArgAsString(3));
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

void Args::handleVec3(glm::vec3* value, const ofxOscMessage &m, int firstArg) {
    if (m.getArgType(firstArg) == OFXOSC_TYPE_STRING) {
        string type = m.getArgAsString(firstArg);
        if (type == "center" || type == "left" || type == "right" || type == "top" || type == "bottom") {
            vector<float> align = parseAlign(m, firstArg);
            *value = glm::vec3(align[0], align[1], align[2]);
        }
    }
    else if (m.getNumArgs() > firstArg+1) {
        *value = glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg+1), m.getNumArgs() > firstArg+2 ? m.getArgAsFloat(firstArg+2) : 0);
    }
    else {
        *value = glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg));
    }
}

void Args::tweenVec3(glm::vec3* value, const ofxOscMessage &m, int firstArg, function<void()> onComplete) {
    createTween(value, glm::vec3(m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg+1), m.getArgAsFloat(firstArg+2)), m.getArgAsFloat(firstArg+3), onComplete);
}

void Args::handleVec3(vector<float>* value, const ofxOscMessage &m, int firstArg) {
    if (m.getArgType(firstArg) == OFXOSC_TYPE_STRING) {
        string type = m.getArgAsString(firstArg);
        if (type == "center" || type == "left" || type == "right" || type == "top" || type == "bottom") {
            vector<float> align = parseAlign(m, firstArg);
            for (int i=0; i<align.size(); i++) {
                (*value)[i] = align[i];
            }
        }
    }
    else if (m.getNumArgs() > firstArg + 3) {
        vector<float> target = {m.getArgAsFloat(firstArg), m.getArgAsFloat(firstArg+1), m.getArgAsFloat(firstArg+2)};
        createTween(value, target, m.getArgAsFloat(firstArg+3));
    }
    else if (m.getNumArgs() > firstArg+1) {
        (*value)[0] = m.getArgAsFloat(firstArg);
        (*value)[1] = m.getArgAsFloat(firstArg+1);
        (*value)[2] = m.getNumArgs() > firstArg+2 ? m.getArgAsFloat(firstArg+2) : 0;
    }
    else {
        (*value)[0] = m.getArgAsFloat(firstArg);
        (*value)[1] = m.getArgAsFloat(firstArg);
        (*value)[2] = m.getArgAsFloat(firstArg);
    }
}

void Args::handleColor(ofFloatColor* value, const ofxOscMessage& m, int firstArg) {
    if (m.getArgType(firstArg) == OFXOSC_TYPE_STRING) {
        if (m.getArgAsString(firstArg) == "lerp") {
            *value = parseLerpColor(m, firstArg);
        }
    }
    else if (m.getNumArgs() > firstArg+1) {
        createTween(value, parseColor(m, firstArg), m.getArgAsFloat(firstArg+3));
    }
    else {
        *value = parseColor(m, firstArg);
    }
}

void Args::handleColor(vector<float>* value, const ofxOscMessage& m, int firstArg) {
    if (m.getArgType(firstArg) == OFXOSC_TYPE_STRING) {
        if (m.getArgAsString(firstArg) == "lerp") {
            ofFloatColor color = parseLerpColor(m, firstArg);
            (*value)[0] = color.r;
            (*value)[1] = color.g;
            (*value)[2] = color.b;
        }
        else {
            // todo: error
        }
    }
    else if (m.getNumArgs() > firstArg+3) {
        ofFloatColor targetColor = parseColor(m, firstArg);
        vector<float> target = {targetColor.r, targetColor.g, targetColor.b};
        createTween(value, target, m.getArgAsFloat(4));
    }
    else {
        ofFloatColor color = parseColor(m, firstArg);
        (*value)[0] = color.r;
        (*value)[1] = color.g;
        (*value)[2] = color.b;
    }
}

bool Args::isTweenVec3(const ofxOscMessage& m, int firstArg) {
    return m.getNumArgs() > firstArg + 3;
}

void Args::createTween(float* value, float target, float dur, function<void()> onComplete, ofxeasing::function ease) {
    Tween<float> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    tween.onComplete = onComplete;
    floatTweens[value] = tween;
}

void Args::createTween(glm::vec3* value, const glm::vec3& target, float dur, function<void()> onComplete, ofxeasing::function ease) {
    Tween<glm::vec3> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    tween.onComplete = onComplete;
    vec3Tweens[value] = tween;
}

void Args::createTween(ofFloatColor* value, const ofFloatColor& target, float dur, function<void()> onComplete, ofxeasing::function ease) {
    Tween<ofFloatColor> tween;
    tween.from = *value;
    tween.to = target;
    tween.dur = dur;
    tween.start = ofGetElapsedTimef();
    tween.ease = ease;
    tween.onComplete = onComplete;
    colorTweens[value] = tween;
}

void Args::createTween(vector<float>* value, const vector<float>& target, float dur, function<void()> onComplete, ofxeasing::function ease) {
    for (int i=0; i<value->size(); i++) {
        createTween(&(*value)[i], target[i], dur, onComplete, ease);
    }
}

