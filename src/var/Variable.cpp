#include "Variable.h"
#include "Args.h"
#include "ColorUtil.h"
#include "TexData.h"

template <typename T>
void Variable<T>::set(T value) {
    this->values.resize(1);
    this->values[0].set(value);
}

template <typename T>
void Variable<T>::set(vector<T> value) {
    this->values.resize(value.size());
    for (int i=0; i<value.size(); i++) {
        this->values[i].set(value[i]);
    }
}

template <typename T>
void Variable<T>::set(const ofxOscMessage& m, int idx) {
    values.resize(m.getNumArgs()-idx);
    for (int i=0; i<values.size(); i++) {
        values[i].set(m, i+idx);
    }
}

template<>
void Variable<ofFloatColor>::set(const ofxOscMessage& m, int idx) {
    int count = m.getArgAsInt(idx);
    values.resize(count);
    for (int i=0; i<values.size(); i++) {
        values[i].set(m, idx+1+i);
    }
}

template<>
void Variable<ofxExprNode>::set(const ofxOscMessage& m, int idx) {
    int count = m.getArgAsInt(idx);
    values.resize(count);
    if ((m.getNumArgs()-idx-1)/3 == count) {
        // every value has different expression
        for (int i=0; i<values.size(); i++) {
            values[i].set(m, idx+1+i*3);
        }
    }
    else {
        // all values have same expression
        for (int i=0; i<values.size(); i++) {
            values[i].set(m, idx+1);
        }
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

template <typename T>
void Variable<T>::update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, TexData* data) {
    for (int i=0; i<values.size(); i++) {
        values[i].update(mics, sounds, notes, i, values.size(), data);
    }
}

template <typename T>
void Variable<T>::afterDraw() {
    for (int i=0; i<values.size(); i++) {
        values[i].afterDraw();
    }
}

template class Variable<float>;
template class Variable<glm::vec3>;
template class Variable<glm::mat4>;
template class Variable<ofFloatColor>;
template class Variable<ofxExprNode>;
