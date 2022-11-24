#include "Variable.h"
#include "Args.h"
#include "ColorUtil.h"

void Variable::init(string type, float value) {
    if (type == "amp" || type == "loud") {
        type = type + ofToString((int)value);
        value = 0.f;
    }
    else if (type == "noise" || type == "rand" || type == "sin" || type == "cos") {
        scale = value;
        value = 0.f;
    }
    this->type = type;
    values.resize(1);
    values[0] = value;
}

void Variable::init(string type, vector<float>& values) {
    init(type, values[0]);
    if (values.size() > 1) {
        this->values = values;
    }
}

void Variable::init(float value) {
    init("const", value);
}

void Variable::init(vector<float> value) {
    init("const", value);
}

void Variable::init(glm::vec3 value) {
    vector<float> vec = {value.x, value.y, value.z};
    init("const", vec);
}

void Variable::init(ofFloatColor value) {
    vector<float> vec = {value.r, value.g, value.b, value.a};
    init("const", vec);
}

void Variable::init(const ofxOscMessage& m) {
    string command = m.getAddress();
    if (command.substr(command.length()-4) == "/var") {
        if (m.getArgType(2) == OFXOSC_TYPE_STRING) {
            vector<float> values;
            for (int i=3; i<m.getNumArgs(); i++) {
                values.push_back(m.getArgAsFloat(i));
            }
            init(m.getArgAsString(1), values);
        }
        else {
            init(m.getArgAsString(1), m.getArgAsFloat(2));
        }
    }
    else if (command.substr(command.length()-10) == "/var/scale") {
        Args::getInstance().handleFloat(&scale, m);
    }
    else if (command.substr(command.length()-10) == "/var/range") {
        Args::getInstance().handleFloat(&rangeFrom, m);
        Args::getInstance().handleFloat(&rangeTo, m);
    }
    else {
        // target, value1, value2, value3
        if (m.getArgType(1) == OFXOSC_TYPE_STRING) {
            string type = m.getArgAsString(1);
            if (type == "lerp") {
                ofFloatColor fromColor = Args::getInstance().parseColor(m, 3);
                ofFloatColor toColor = Args::getInstance().parseColor(m, 6);
                float perc = m.getArgAsFloat(2);
                ofFloatColor color = ofxColorTheory::ColorUtil::lerpLch(fromColor, toColor, perc);
                vector<float> vec = {color.r, color.g, color.b};
                init("const", vec);
            }
            else if (type == "center" || type == "left" || type == "right" || type == "top" || type == "bottom") {
                string argH = m.getArgAsString(1);
                string argV = m.getNumArgs() > 2 ? m.getArgAsString(2) : argH;
                vector<float> values = {0, 0, 0};
                if (argH == "center") {
                    values[0] = 0.5f;
                }
                else if (argH == "left") {
                    values[0] = 0.f;
                }
                else if (argH == "right") {
                    values[0] = 1.f;
                }
                if (argV == "center") {
                    values[1] = 0.5f;
                }
                else if (argV == "top") {
                    values[1] = 0.f;
                }
                else if (argV == "bottom") {
                    values[1] = 1.f;
                }
                init("const", values);
            }
            else {
                vector<float> values;
                for (int i=2; i<m.getNumArgs(); i++) {
                    values.push_back(m.getArgAsFloat(i));
                }
                init(type, values);
            }
        }
        else {
            if (isColor) {
                Args::getInstance().handleColor(&values, m);
            }
            else if (isVec3) {
                Args::getInstance().handleVec3(&values, m);
            }
        }
    }
}

void Variable::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    if (type.substr(0, 5) == "tidal") {
        for (int i = 0; i < notes.size(); i++) {
            if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
                //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
                float diff = ofGetElapsedTimef() - notes[i].timeStamp;
                if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                    int instNum = notes[i].instNum;
                    if (type.substr(0, 6) == "tidal" + ofToString(instNum)) {
                        if (type.substr(6) == "") {
                            values[0] += notes[i].gain;
                        }
                        else if (type.substr(6) == "amp") {
                            // e.g. tidal0amp
                            values[0] += notes[i].amp;
                        }
                        else if (type.substr(6) == "onset") {
                            // e.g. tidal0onset
                            values[0] = 1;
                        }
                    }
                }
            }
        }
    }
    else {
        updateValue(sounds);
    }
}

float Variable::updateValue(const vector<Sound> &sounds, int idx) {
    float value = 0;
    if (type.substr(0, 3) == "amp" || type.substr(0, 4) == "loud" || type.substr(0, 5) == "onset" || type.substr(0, 4) == "mfcc") {
        int j = ofToInt(type.substr(type.length()-1));
        if (sounds.size() > j) {
            if (type == "amp") {
                values[idx] = sounds[j].amplitude;
            }
            else if (type == "loud") {
                values[idx] = sounds[j].loudness;
            }
            else if (type == "onset") {
                values[idx] = sounds[j].onset;
            }
            else {
                values = sounds[j].mfcc;
            }
        }
        else {
            ofLog() << "sound data not available " + type + ":" + ofToString(value);
        }
    }
    else {
        if (type == "noise") {
            values[idx] = ofNoise((int)(size_t)this, ofGetElapsedTimef());
        }
        else if (type == "rand") {
            values[idx] = ofRandom(1.f);
        }
        else if (type == "sin") {
            values[idx] = abs(sin(ofGetElapsedTimef()));
        }
        else if (type == "cos") {
            values[idx] = abs(cos(ofGetElapsedTimef()));
        }
    }
}
