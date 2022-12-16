#include "Value.h"
#include "Args.h"
#include "Config.h"
#include "TexData.h"
#include "VariablePool.h"

template<typename T>
Value<T>::Value() {
}

template<typename T>
T& Value<T>::get() {
    return value;
}

template<typename T>
T Value<T>::get() const {
    return value;
}

template<typename T>
void Value<T>::set(const string& expr) {
    type = "expr";
    this->expr.set(expr);
    addSharedVars();
    this->value = this->expr.get();
}

template<typename T>
void Value<T>::set(const vector<string>& expr, int i) {
    type = "expr";
    this->expr.set(expr, i);
    addSharedVars();
//    this->value = this->expr.get();
}

template<typename T>
void Value<T>::addSharedVars() {
    map<string, BaseVar*>& pool = VariablePool::getPool();
    for (map<string, BaseVar*>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        Variable<float>* floatVar = dynamic_cast<Variable<float>*>(it->second);
        if (floatVar != NULL) {
            this->expr.addVar(it->first, floatVar->get());
        }
    }
}

// todo: fix
//template<>
//void Value<float>::set(const string& type) {
//    float value = 0.f;
//    if (type.substr(0, 3) == "mic" || type.substr(0, 3) == "amp" || type.substr(0, 4) == "loud" || type.substr(0, 5) == "onset" || type.substr(0, 4) == "mfcc") {
//        size_t col = type.find(":");
//        if (col != std::string::npos) {
//            chan = ofToInt(type.substr(col+1));
//            this->type = type.substr(0, col);
//        }
//        else {
//            chan = ofToInt(type.substr(type.length()-1));
//            this->type = type.substr(0, type.length()-1);
//        }
//    }
//    else if (type.substr(0, 5) == "tidal") {
//        size_t col = type.find(":");
//        if (col != std::string::npos) {
//            chan = ofToInt(type.substr(col+1));
//            subtype = type.substr(col+2); // todo: support "10"
//        }
//        else {
//            chan = type.length() > 5 ? ofToInt(type.substr(5, 1)) : 0;
//            subtype = type.substr(6);
//        }
//        this->type = "tidal";
//        // todo: validate subtype
//    }
//    else if (DataSourceMap.find(type) != DataSourceMap.end()) {
//        this->type = type;
//    }
//    else {
//        ofLogError() << ("invalid var type: " + type);
//    }
//    this->value = value;
//}

template <typename T>
void Value<T>::set(const T& value) {
    type = "const";
    this->value = value;
}

template<>
void Value<float>::set(const ofxOscMessage& m, int i) {
    if (m.getArgType(i) == OFXOSC_TYPE_STRING) {
        set(m.getArgAsString(i));
    }
    else {
        set(m.getArgAsFloat(i));
    }
}

template<>
void Value<glm::vec3>::set(const ofxOscMessage& m, int i) {
    try {
        set(Args::parseVec3(m, i));
    }
    catch (...) {
        set(Args::parseVec3Expr(m, i));
    }
}

template<>
void Value<glm::mat4>::set(const ofxOscMessage& m, int i) {
    throw "Value<glm::mat4>::set not implemented";
}

template<>
void Value<ofFloatColor>::set(const ofxOscMessage& m, int i) {
    try {
        set(Args::parseColor(m, i));
    }
    catch (...) {
        set(Args::parseColorExpr(m, i));
    }
}

template<>
void Value<ofxExprNode>::set(const ofxOscMessage& m, int i) {
    vector<string> pos = Args::parseVec3Expr(m, i);
    set(pos);
    vector<string> rota = Args::parseVec3Expr(m, i+1);
    set(rota, 3);
    vector<string> scale = Args::parseVec3Expr(m, i+2);
    set(scale, 6);
}

template<typename T>
void Value<T>::update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, int index, int total, TexData* data) {
    if (type == "expr") {
        value = expr.get();
    }
}

template<>
void Value<float>::update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, int index, int total, TexData* data) {
    if (type == "expr") {
        value = expr.get();
    }
    else if (type == "tidal") {
        if (subtype == "onset") {
            value = 0;
        }
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
        float time = (data != NULL ? data->time : ofGetElapsedTimef()) * speed;
        if (type == "time") {
            value = fmod(time, 1.f);
        }
        else if (type == "noise") {
            value = ofNoise((int)(size_t)this, time);
        }
        else if (type == "rand") {
            value = ofRandom(1.f);
        }
        else if (type == "sin") {
            value = (sin(time)+1)/2.f;
        }
        else if (type == "cos") {
            value = (cos(time)+1)/2.f;
        }
        else if (type == "amp" || type == "loud" || type == "onset" || type == "mfcc") {
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
                ofLogError() << ("sound data not available " + type + ":" + ofToString(chan));
            }
        }
        else if (type == "mic") {
            if (mics.size() > chan) {
                value = mics[chan].amplitude;
            }
            else {
                value = 0;
                ofLogError() << ("mic not available:" + ofToString(chan));
            }
        }
    }
}

template<typename T>
void Value<T>::afterDraw() {
}

template<>
void Value<float>::afterDraw() {
    if (type == "tidal") {
        value -= 1.f/8.f;
        if (value < 0) {
            value = 0;
        }
    }
}

template class Value<float>;
template class Value<glm::vec3>;
template class Value<glm::mat4>;
template class Value<ofFloatColor>;
template class Value<ofxExprNode>;
