#include "Variable.h"
#include "Args.h"
#include "ColorUtil.h"
#include "TexData.h"
#include "VariablePool.h"

template <typename T>
void Variable<T>::set(T value) {
    type = "const";
    this->values.resize(1);
    this->values[0].set(value);
}

template <typename T>
void Variable<T>::set(vector<T> value) {
    type = "const";
    this->values.resize(value.size());
    for (int i=0; i<value.size(); i++) {
        this->values[i].set(value[i]);
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
    map<string, BaseVar*>& pool = VariablePool::getPool();
    for (map<string, BaseVar*>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        Variable<float>* floatVar = dynamic_cast<Variable<float>*>(it->second);
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
        int count = m.getNumArgs() > (idx+1) ? m.getArgAsInt(idx+1) : 1;
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

template <typename T>
void Variable<T>::update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, TexData* data) {
    if (type == "expr") {
        float i = 0;
        float total = values.size();
        expr.addVar("i", i);
        expr.addVar("total", total);
        // todo: add mics, sounds notes, data to expr vars
        for (i; i<values.size(); i++) {
            values[i].set(expr.get());
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
            float val = values[i].get() - 1.f/8.f;
            if (val < 0.f) {
                val = 0.f;
            }
            values[i].set(val);
        }
    }
}

template class Variable<float>;
template class Variable<glm::vec3>;
template class Variable<glm::mat4>;
template class Variable<ofFloatColor>;
template class Variable<ofxExprNode>;
