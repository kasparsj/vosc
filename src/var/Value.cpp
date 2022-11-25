#include "Value.h"
#include "Args.h"

void Value::set(string type) {
    if (type.substr(0, 3) == "amp" || type.substr(0, 4) == "loud" || type.substr(0, 5) == "onset" || type.substr(0, 4) == "mfcc") {
        size_t col = type.find(":");
        if (col != std::string::npos) {
            chan = ofToInt(type.substr(col+1));
            type = type.substr(0, col);
        }
        else {
            chan = ofToInt(type.substr(type.length()-1));
            type = type.substr(0, type.length()-1);
        }
    }
    else if (type.substr(0, 5) == "tidal") {
        size_t col = type.find(":");
        if (col != std::string::npos) {
            chan = ofToInt(type.substr(col+1));
            subtype = type.substr(col+2); // todo: support "10"
        }
        else {
            chan = ofToInt(type.substr(5, 1));
            subtype = type.substr(6);
        }
        type = "tidal";
    }
    else {
        ofLog() << "invalid var type: " + type;
    }
    this->type = type;
    this->value = 0.f;
}

void Value::set(float value) {
    this->type = "const";
    this->value = value;
}

void Value::set(const ofxOscMessage& m, int i) {
    if (m.getNumArgs() > (i+1)) {
        type = "const";
        if (m.getNumArgs() > (i+2)) {
            Args::getInstance().createTween(&value, m.getArgAsFloat(i), m.getArgAsFloat(i+1), m.getArgAsString(i+2));
        }
        else {
            Args::getInstance().createTween(&value, m.getArgAsFloat(i), m.getArgAsFloat(i+1));
        }
    }
    else {
        if (m.getArgType(i) == OFXOSC_TYPE_STRING) {
            set(m.getArgAsString(i));
        }
        else {
            set(m.getArgAsFloat(i));
        }
    }
}

void Value::update(const vector<Sound> &sounds, const vector<TidalNote> &notes, int index, int total) {
    if (type == "tidal") {
        for (int i = 0; i < notes.size(); i++) {
            if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
                //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
                float diff = ofGetElapsedTimef() - notes[i].timeStamp;
                if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                    int instNum = notes[i].instNum;
                    if (chan == instNum) {
                        if (subtype == "" || subtype == "gain") {
                            value += notes[i].gain;
                        }
                        else if (subtype == "amp") {
                            // e.g. tidal0amp
                            value += notes[i].amp;
                        }
                        else if (subtype == "onset") {
                            // e.g. tidal0onset
                            value = 1;
                        }
                    }
                }
            }
        }
    }
    else {
        update(sounds, index, total);
    }
}

void Value::update(const vector<Sound> &sounds, int index, int total) {
    if (type == "amp" || type == "loud" || type == "onset" || type == "mfcc") {
        if (sounds.size() > chan) {
            if (type == "amp") {
                value = sounds[chan].amplitude;
            }
            else if (type == "loud") {
                value = sounds[chan].loudness;
            }
            else if (type == "onset") {
                value = sounds[chan].onset;
            }
            else {
                value = 0;
                const vector<float>& mfcc = sounds[chan].mfcc;
                int k = (mfcc.size() / total);
                for (int i=index*k; i<index*k+k; i++) {
                    value += mfcc[i];
                }
                value /= k;
            }
        }
        else {
            value = 0;
            ofLog() << "sound data not available " + type + ":" + ofToString(value);
        }
    }
    else {
        if (type == "noise") {
            value = ofNoise((int)(size_t)this, ofGetElapsedTimef());
        }
        else if (type == "rand") {
            value = ofRandom(1.f);
        }
        else if (type == "sin") {
            value = abs(sin(ofGetElapsedTimef()));
        }
        else if (type == "cos") {
            value = abs(cos(ofGetElapsedTimef()));
        }
    }
}

void Value::afterDraw() {
    if (type == "tidal") {
        value -= 1.f/8.f;
        if (value < 0) {
            value = 0;
        }
    }
}