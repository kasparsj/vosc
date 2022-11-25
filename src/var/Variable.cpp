#include "Variable.h"
#include "Args.h"
#include "ColorUtil.h"
#include "Layer.h"

void Variable::set(float value) {
    this->values.resize(1);
    this->values[0].set(value);
}

void Variable::set(vector<float> value) {
    this->values.resize(value.size());
    for (int i=0; i<value.size(); i++) {
        this->values[i].set(value[i]);
    }
}

void Variable::set(glm::vec3 value) {
    vector<float> vec = {value.x, value.y, value.z};
    set(vec);
}

void Variable::set(ofFloatColor value) {
    vector<float> vec = {value.r, value.g, value.b, value.a};
    set(vec);
}

void Variable::set(const ofxOscMessage& m, int idx) {
    string command = m.getAddress();
    if (command.length() >= 4 && command.substr(command.length()-4) == "/var") {
        values.resize(m.getNumArgs() - idx);
        for (int i=idx; i<m.getNumArgs(); i++) {
            if (m.getArgType(i) == OFXOSC_TYPE_STRING) {
                values[(i-idx)].set(m.getArgAsString(i));
            }
            else {
                values[(i-idx)].set(m.getArgAsFloat(i));
            }
        }
    }
    else if (command.length() >= 10 && command.substr(command.length()-10) == "/var/range") {
        for  (int i=0; i<values.size(); i++) {
            if (m.getNumArgs() == idx + 1) {
                Args::getInstance().handleFloat(&values[i].rangeFrom, m, idx);
            }
            else if (m.getNumArgs() == idx + 2) {
                if (values.size() == 2) {
                    Args::getInstance().handleFloat(&values[i].rangeFrom, m, idx+i);
                }
                else {
                    Args::getInstance().handleFloat(&values[i].rangeFrom, m, idx);
                    Args::getInstance().handleFloat(&values[i].rangeTo, m, idx+1);
                }
            }
            else if (m.getNumArgs() == idx + values.size()*2) {
                Args::getInstance().handleFloat(&values[i].rangeFrom, m, idx+i);
                Args::getInstance().handleFloat(&values[i].rangeTo, m, idx+values.size()+i);
            }
            else {
                ofLog() << "unsupported /var/range format";
            }
        }
    }
    else if (command.length() >= 10 && command.substr(command.length()-10) == "/var/speed") {
        for  (int i=0; i<values.size(); i++) {
            if (m.getNumArgs() == idx + 1) {
                Args::getInstance().handleFloat(&values[i].speed, m, idx);
            }
            else if (m.getNumArgs() == idx + values.size()) {
                Args::getInstance().handleFloat(&values[i].speed, m, idx+i);
            }
            else {
                ofLog() << "unsupported /var/speed format";
            }
        }
    }
    else {
        // target, value1, value2, value3
        if (isColor) {
            setColor(m, idx);
        }
        else if (isVec3) {
            setVec3(m, idx);
        }
        else {
            setFloat(m, idx);
        }
    }
}

void Variable::setColor(const ofxOscMessage& m, int idx) {
    if (m.getArgType(idx) == OFXOSC_TYPE_STRING) {
        if (m.getArgAsString(idx) == "lerp") {
            ofFloatColor color = Args::getInstance().parseLerpColor(m, idx);
            values[0].set(color.r);
            values[1].set(color.r);
            values[2].set(color.r);
        }
        else {
            for (int i=(idx+1); i<m.getNumArgs(); i++) {
                if (m.getArgType(i) == OFXOSC_TYPE_STRING) {
                    values[i-(idx+1)].set(m.getArgAsString(i));
                }
                else {
                    values[i-(idx+1)].set(m.getArgAsFloat(i));
                }
            }
        }
    }
    else if (m.getNumArgs() > idx+3) {
        ofFloatColor targetColor = Args::getInstance().parseColor(m, idx);
        vector<float> target = {targetColor.r, targetColor.g, targetColor.b};
        tween(target, m.getArgAsFloat(4));
    }
    else {
        ofFloatColor color = Args::getInstance().parseColor(m, idx);
        values[0].set(color.r);
        values[1].set(color.g);
        values[2].set(color.b);
    }
}

void Variable::setVec3(const ofxOscMessage &m, int idx) {
    values.resize(3);
    if (m.getNumArgs() == idx + 4) { // 4 args
        vector<float> target = {m.getArgAsFloat(idx), m.getArgAsFloat(idx+1), m.getArgAsFloat(idx+2)};
        tween(target, m.getArgAsFloat(idx+3));
    }
    else if (m.getNumArgs() == idx + 3) { // 3 args
        for (int i=idx; i<m.getNumArgs(); i++) {
            if (m.getArgType(i) == OFXOSC_TYPE_STRING) {
                values[i-idx].set(m.getArgAsString(i));
            }
            else {
                values[i-idx].set(m.getArgAsFloat(i));
            }
        }
    }
    else if (m.getNumArgs() == idx + 2 && m.getArgType(idx) == OFXOSC_TYPE_STRING) { // 2 args
        string type = m.getArgAsString(idx);
        if (type == "center" || type == "left" || type == "right" || type == "top" || type == "bottom") {
            vector<float> align = Args::getInstance().parseAlign(m, idx);
            for (int i=0; i<align.size(); i++) {
                values[i].set(align[i]);
            }
        }
    }
    else if (m.getNumArgs() == idx + 1) { // 1 arg
        for (int i=0; i<m.getNumArgs(); i++) {
            if (m.getArgType(idx) == OFXOSC_TYPE_STRING) {
                values[i].set(m.getArgAsString(idx));
            }
            else {
                values[i].set(m.getArgAsFloat(idx));
            }
        }
    }
    else {
        ofLog() << "setVec3 unsupported format";
    }
}

void Variable::setFloat(const ofxOscMessage& m, int idx) {
    values.resize(m.getNumArgs()-idx);
    for (int i=0; i<values.size(); i++) {
        values[i].set(m, i+idx);
    }
}

void Variable::tween(const vector<float>& target, float dur, ofxeasing::function ease) {
    for (int i=0; i<values.size(); i++) {
        values[i].tween(target[i], dur, ease);
    }
}

void Variable::update(const vector<Sound> &sounds, const vector<TidalNote> &notes, Layer* layer) {
    for (int i=0; i<values.size(); i++) {
        values[i].update(sounds, notes, i, values.size(), layer);
    }
}

void Variable::afterDraw() {
    for (int i=0; i<values.size(); i++) {
        values[i].afterDraw();
    }
}
