#include "Variable.h"
#include "Args.h"
#include "ColorUtil.h"
#include "TexData.h"
#include "VariablePool.h"

template <typename T>
void Variable<T>::set(T value) {
    type = "const";
    this->values.resize(1);
    this->values[0] = value;
}

template <typename T>
void Variable<T>::set(vector<T> value) {
    type = "const";
    this->values.resize(value.size());
    for (int i=0; i<value.size(); i++) {
        this->values[i] = value[i];
    }
}

template<typename T>
void Variable<T>::set(const string& expr) {
    type = "expr";
    this->expr.set(expr);
    addSharedVars();
}

template<typename T>
void Variable<T>::set(const vector<string>& expr) {
    type = "expr";
    this->expr.set(expr);
    addSharedVars();
}

template<typename T>
void Variable<T>::addSharedVars() {
    map<string, shared_ptr<BaseVar>>& pool = VariablePool::getPool(NULL);
    for (map<string, shared_ptr<BaseVar>>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        Variable<float>* floatVar = dynamic_cast<Variable<float>*>(it->second.get());
        if (floatVar != NULL) {
            expr.addVar(it->first, floatVar->get());
        }
    }
}

template<typename T>
void Variable<T>::set(const ofxOscMessage& m, int idx) {
    try {
        set(Args::parseConst<T>(m, idx));
    }
    catch (...) {
        set(Args::parseExpr<T>(m, idx));
        int count = m.getNumArgs() > idx ? m.getArgAsInt(idx) : 1;
        values.resize(count);
    }
}

//void Variable::setColor(const ofxOscMessage& m, int idx) {
//    if (m.getNumArgs() > idx+3) { // args > 3
//        if (m.getArgType(idx) == OFXOSC_TYPE_STRING && m.getArgAsString(idx) == "lerp") {
//            ofFloatColor color = Args::get().parseLerpColor(m, idx);
//            values[0].set(color.r);
//            values[1].set(color.r);
//            values[2].set(color.r);
//        }
//        else {
//            ofFloatColor targetColor = Args::get().parseColor(m, idx);
//            vector<float> target = {targetColor.r, targetColor.g, targetColor.b};
//            tween(target, m.getArgAsFloat(4));
//        }
//    }
//    else if (m.getNumArgs() == idx + 3) { // args == 3
//        for (int i=(idx+1); i<m.getNumArgs(); i++) {
//            if (m.getArgType(i) == OFXOSC_TYPE_STRING) {
//                values[i-(idx+1)].set(m.getArgAsString(i));
//            }
//            else {
//                values[i-(idx+1)].set(Args::get().parseIntOrFloat(m, i));
//            }
//        }
//    }
//    else if (m.getNumArgs() == idx + 1) { // args == 1
//        for (int i=0; i<3; i++) {
//            if (m.getArgType(idx) == OFXOSC_TYPE_STRING) {
//                values[i].set(m.getArgAsString(idx));
//            }
//            else {
//                values[i].set(Args::get().parseIntOrFloat(m, idx));
//            }
//        }
//    }
//    else {
//        ofLogError() << (m.getAddress() + " : invalid color format");
//    }
//}
//
//void Variable::setVec3(const ofxOscMessage &m, int idx) {
//    values.resize(3);
//    if (m.getNumArgs() == idx + 3) { // 3 args
//        for (int i=idx; i<m.getNumArgs(); i++) {
//            if (m.getArgType(i) == OFXOSC_TYPE_STRING) {
//                values[i-idx].set(m.getArgAsString(i));
//            }
//            else {
//                values[i-idx].set(m.getArgAsFloat(i));
//            }
//        }
//    }
//    else if (m.getNumArgs() == idx + 2 && m.getArgType(idx) == OFXOSC_TYPE_STRING) { // 2 args
//        string type = m.getArgAsString(idx);
//        if (type == "center" || type == "left" || type == "right" || type == "top" || type == "bottom") {
//            vector<float> align = Args::get().parseAlign(m, idx);
//            for (int i=0; i<align.size(); i++) {
//                values[i].set(align[i]);
//            }
//        }
//    }
//    else if (m.getNumArgs() == idx + 1) { // 1 arg
//        for (int i=0; i<m.getNumArgs(); i++) {
//            if (m.getArgType(idx) == OFXOSC_TYPE_STRING) {
//                values[i].set(m.getArgAsString(idx));
//            }
//            else {
//                values[i].set(m.getArgAsFloat(idx));
//            }
//        }
//    }
//    else {
//        ofLogError() << ("setVec3 unsupported format");
//    }
//}

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

//template<>
//void Value<float>::update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, int index, int total, TexData* data) {
//    if (type == "expr") {
//        value = expr.get();
//    }
//    else if (type == "tidal") {
//        if (subtype == "onset") {
//            value = 0;
//        }
//        for (int i = 0; i < notes.size(); i++) {
//            if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
//                //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
//                float diff = ofGetElapsedTimef() - notes[i].timeStamp;
//                if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
//                    int instNum = notes[i].instNum;
//                    if (chan == instNum) {
//                        if (subtype == "" || subtype == "gain") {
//                            value += notes[i].gain;
//                        }
//                        else if (subtype == "amp") {
//                            // e.g. tidal0amp
//                            value += notes[i].amp;
//                        }
//                        else if (subtype == "onset") {
//                            // e.g. tidal0onset
//                            value = 1;
//                        }
//                    }
//                }
//            }
//        }
//    }
//    else {
//        float time = (data != NULL ? data->time : ofGetElapsedTimef()) * speed;
//        if (type == "time") {
//            value = fmod(time, 1.f);
//        }
//        else if (type == "noise") {
//            value = ofNoise((int)(size_t)this, time);
//        }
//        else if (type == "rand") {
//            value = ofRandom(1.f);
//        }
//        else if (type == "sin") {
//            value = (sin(time)+1)/2.f;
//        }
//        else if (type == "cos") {
//            value = (cos(time)+1)/2.f;
//        }
//        else if (type == "amp" || type == "loud" || type == "onset" || type == "mfcc") {
//            if (sounds.size() > chan) {
//                if (type == "amp") {
//                    value = sounds[chan].amplitude;
//                }
//                else if (type == "loud") {
//                    value = sounds[chan].loudness;
//                }
//                else if (type == "onset") {
//                    value = sounds[chan].onset;
//                }
//                else {
//                    value = 0;
//                    const vector<float>& mfcc = sounds[chan].mfcc;
//                    int k = (mfcc.size() / total);
//                    for (int i=index*k; i<index*k+k; i++) {
//                        value += mfcc[i];
//                    }
//                    value /= k;
//                }
//            }
//            else {
//                value = 0;
//                ofLogError() << ("sound data not available " + type + ":" + ofToString(chan));
//            }
//        }
//        else if (type == "mic") {
//            if (mics.size() > chan) {
//                value = mics[chan].amplitude;
//            }
//            else {
//                value = 0;
//                ofLogError() << ("mic not available:" + ofToString(chan));
//            }
//        }
//    }
//}


template <typename T>
void Variable<T>::update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, TexData* data) {
    if (type == "expr") {
        float i = 0;
        float total = values.size();
        expr.addVar("i", i);
        expr.addVar("total", total);
        // todo: add mics, sounds notes, data to expr vars
        for (i; i<values.size(); i++) {
            values[i] = expr.get();
        }
    }
}

template <typename T>
void Variable<T>::afterDraw() {
}

template<>
void Variable<float>::afterDraw() {
    if (type == "tidal") {
        for (int i=0; i<values.size(); i++) {
            float val = values[i] - 1.f/8.f;
            if (val < 0.f) {
                val = 0.f;
            }
            values[i] = val;
        }
    }
}

template<typename T>
BufData Variable<T>::asBufferData() {
    GLsizeiptr size = values.size()*sizeof(T);
    return {size, &values[0]};
}

template<>
BufData Variable<ofxExprNode>::asBufferData() {
    GLsizeiptr size = values.size()*sizeof(glm::mat4);
    matrices.resize(values.size());
    for (int i=0; i<values.size(); i++) {
        matrices[i] = values[i].getLocalTransformMatrix();
    }
    return {size, &matrices[0]};
}

template class Variable<float>;
template class Variable<glm::vec3>;
template class Variable<glm::mat4>;
template class Variable<ofFloatColor>;
template class Variable<ofxExprNode>;
