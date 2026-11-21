#include "Variable.h"
#include "ComponentLiteral.hpp"
#include "Args.h"
#include "ColorUtil.h"
#include "TexData.h"
#include "VariablePool.h"
#include "Inputs.hpp"

namespace {
template<typename T>
constexpr bool supportsComponentExpressions() {
    return std::is_same<T, glm::vec2>::value || std::is_same<T, glm::vec3>::value ||
        std::is_same<T, ofFloatColor>::value;
}

template<typename T>
float* componentData(T& value) {
    return glm::value_ptr(value);
}

template<>
float* componentData(ofFloatColor& value) {
    return &value.r;
}

template<typename T>
size_t componentCount() {
    return sizeof(T) / sizeof(float);
}

template<typename Expr>
void addExpressionVariable(Expr& expressions, const string& name, float& value,
                           bool recompile = true, bool setTime = true) {
    for (size_t index = 0; index < expressions.size(); ++index)
        if (expressions[index]) expressions[index]->addVar(name, value, recompile, setTime);
}

template<typename Expr>
void addExpressionConstant(Expr& expressions, const string& name, float value,
                           bool recompile = true) {
    for (size_t index = 0; index < expressions.size(); ++index)
        if (expressions[index]) expressions[index]->addConst(name, value, recompile);
}
}

template <typename T>
void Variable<T>::set(T value) {
    _isExpr = false;
    expressionMask.clear();
    configuredExpressions.clear();
    componentExpressions.clear();
    componentExpressionMode = false;
    this->values.resize(1);
    this->values[0] = value;
}

template <typename T>
void Variable<T>::set(vector<T> value) {
    _isExpr = false;
    expressionMask.clear();
    configuredExpressions.clear();
    componentExpressions.clear();
    componentExpressionMode = false;
    this->values.resize(value.size());
    for (int i=0; i<value.size(); i++) {
        this->values[i] = value[i];
    }
}

template<typename T>
void Variable<T>::set(const string& expr) {
    _isExpr = true;
    expressionMask.assign(1, true);
    configuredExpressions.clear();
    componentExpressions.clear();
    componentExpressionMode = std::is_same<T, int>::value;
    if constexpr (std::is_same<T, int>::value) {
        configuredExpressions = {expr};
        componentExpressions = {std::make_shared<ofxExpr_<float>>()};
        componentExpressions.front()->set(expr);
        values.resize(1);
    } else {
        this->expr.set(expr);
        values.resize(1);
    }
    addSharedVars();
}

template<typename T>
void Variable<T>::set(const vector<string>& expr) {
    set(expr, vector<bool>(expr.size(), true));
}

template<typename T>
void Variable<T>::set(const vector<string>& expressions, const vector<bool>& mask) {
    _isExpr = true;
    expressionMask = mask.size() == expressions.size() ? mask : vector<bool>(expressions.size(), true);
    componentExpressionMode = std::is_same<T, int>::value || supportsComponentExpressions<T>();
    if constexpr (std::is_same<T, int>::value) {
        configuredExpressions = expressions;
        componentExpressions.clear();
        values.resize(expressions.size());
        for (size_t index = 0; index < expressions.size(); ++index) {
            if (expressionMask[index]) {
                auto component = std::make_shared<ofxExpr_<float>>();
                component->set(expressions[index]);
                componentExpressions.push_back(std::move(component));
            } else {
                size_t consumed = 0;
                const long long value = std::stoll(expressions[index], &consumed);
                if (consumed != expressions[index].size() ||
                    value < std::numeric_limits<int>::min() || value > std::numeric_limits<int>::max())
                    throw std::out_of_range("integer recipe component is outside int32 range");
                values[index] = static_cast<int>(value);
                componentExpressions.push_back(nullptr);
            }
        }
    } else if constexpr (supportsComponentExpressions<T>()) {
        if (expressions.size() != componentCount<T>())
            throw std::invalid_argument("vector expression component count mismatch");
        configuredExpressions = expressions;
        componentExpressions.clear();
        componentExpressions.reserve(expressions.size());
        values.assign(1, T{});
        float* components = componentData(values.front());
        for (size_t index = 0; index < expressions.size(); ++index) {
            if (expressionMask[index]) {
                auto component = std::make_shared<ofxExpr_<float>>();
                component->set(expressions[index]);
                componentExpressions.push_back(std::move(component));
            } else {
                const auto parsed = parseComponentLiteral(expressions[index]);
                if (!parsed || std::abs(*parsed) > std::numeric_limits<float>::max())
                    throw std::out_of_range("vector recipe component is invalid");
                components[index] = static_cast<float>(*parsed);
                componentExpressions.push_back(nullptr);
            }
        }
    } else {
        values.resize(std::is_arithmetic<T>::value ? expressions.size() : 1);
        this->expr.set(expressions);
    }
    addSharedVars();
}

template<typename T>
bool Variable<T>::validateComponentExpressions() {
    for (const auto& component : componentExpressions) {
        if (!component) continue;
        if (component->getExpression().find('\0') != std::string::npos ||
            !component->compile(false)) return false;
        try {
            // Parse with the same symbols and functions, without running optimizer callbacks.
            auto parser = component->getParser();
            parser.EnableOptimizer(false);
            for (const auto& [name, binding] : parser.GetUsedVar()) {
                // The per-frame update binds these before evaluating the component.
                if (!binding && name != "i" && name != "total") return false;
            }
        } catch (const mu::Parser::exception_type&) {
            return false;
        }
    }
    return true;
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
        if constexpr (std::is_same<T, int>::value) {
            if (count > 1 && componentExpressions.size() == 1) {
                componentExpressions.resize(count, componentExpressions.front());
                configuredExpressions.resize(count, configuredExpressions.front());
                expressionMask.assign(count, true);
            }
        }
    }
}

template<typename T>
void Variable<T>::addSharedVars() {
    if (componentExpressionMode) {
        const auto& pool = VariablePool::sharedVariables();
        for (const auto& kv : pool) {
            Variable<float>* floatVar = dynamic_cast<Variable<float>*>(kv.second.get());
            if (floatVar != NULL) addExpressionVariable(componentExpressions, kv.first, floatVar->get());
        }
        const auto& inputs = Inputs::get().all();
        for (const auto& kv : inputs)
            addExpressionVariable(componentExpressions, "in_" + kv.first, kv.second->get());
        addExpressionConstant(componentExpressions, "left", 0.f);
        addExpressionConstant(componentExpressions, "right", 1.f);
        addExpressionConstant(componentExpressions, "top", 0.f);
        addExpressionConstant(componentExpressions, "bottom", 1.f);
        addExpressionConstant(componentExpressions, "center", 0.5f);
        return;
    }
    const auto& pool = VariablePool::sharedVariables();
    for (const auto& kv : pool) {
        Variable<float>* floatVar = dynamic_cast<Variable<float>*>(kv.second.get());
        if (floatVar != NULL) {
            addExpressionVariable(expr, kv.first, floatVar->get());
        }
    }
    const auto& inputs = Inputs::get().all();
    for (const auto& kv : inputs) {
        // todo: support other properties (e.g. loudness, raw amplitude)
        addExpressionVariable(expr, "in_" + kv.first, kv.second->get());
    }
    addExpressionConstant(expr, "left", 0.f);
    addExpressionConstant(expr, "right", 1.f);
    addExpressionConstant(expr, "top", 0.f);
    addExpressionConstant(expr, "bottom", 1.f);
    addExpressionConstant(expr, "center", 0.5f);
}

template<>
void Variable<int>::addSharedVars() {
    // For int variables, use float expressions and cast to int
    const auto& pool = VariablePool::sharedVariables();
    for (const auto& kv : pool) {
        Variable<float>* floatVar = dynamic_cast<Variable<float>*>(kv.second.get());
        if (floatVar != NULL) {
            addExpressionVariable(componentExpressions, kv.first, floatVar->get());
        }
    }
    const auto& inputs = Inputs::get().all();
    for (const auto& kv : inputs) {
        addExpressionVariable(componentExpressions, "in_" + kv.first, kv.second->get());
    }
    addExpressionConstant(componentExpressions, "left", 0.f);
    addExpressionConstant(componentExpressions, "right", 1.f);
    addExpressionConstant(componentExpressions, "top", 0.f);
    addExpressionConstant(componentExpressions, "bottom", 1.f);
    addExpressionConstant(componentExpressions, "center", 0.5f);
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

//template<>
//void Value<float>::update(const vector<TidalNote> &notes, int index, int total, TexData* data) {
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
//            if (inputs.size() > chan) {
//                value = inputs[chan].amplitude;
//            }
//            else {
//                value = 0;
//                ofLogError() << ("mic not available:" + ofToString(chan));
//            }
//        }
//    }
//}


template <typename T>
void Variable<T>::update() {
    if (_isExpr) {
        float i = 0;
        float total = values.size();
        if constexpr (supportsComponentExpressions<T>()) {
            if (componentExpressionMode) {
                addExpressionVariable(componentExpressions, "i", i, true, false);
                addExpressionVariable(componentExpressions, "total", total, true, false);
                for (; i < values.size(); ++i) {
                    float* components = componentData(values[static_cast<size_t>(i)]);
                    for (size_t index = 0; index < componentExpressions.size(); ++index)
                        if (expressionMask[index]) components[index] = componentExpressions[index]->get();
                }
                return;
            }
        }
        addExpressionVariable(expr, "i", i, true, false);
        addExpressionVariable(expr, "total", total, true, false);
        // todo: add mics, sounds notes, data to expr vars
        for (i; i<values.size(); i++) {
            values[i] = expr.get();
        }
    }
}

template<>
void Variable<int>::update() {
    if (_isExpr) {
        float i = 0;
        float total = values.size();
        addExpressionVariable(componentExpressions, "i", i, true, false);
        addExpressionVariable(componentExpressions, "total", total, true, false);
        // todo: add mics, sounds notes, data to expr vars
        for (i; i<values.size(); i++) {
            if (!expressionMask[i]) continue;
            const double evaluated = static_cast<double>(componentExpressions[i]->get());
            if (!std::isfinite(evaluated) ||
                evaluated < static_cast<double>(std::numeric_limits<int>::min()) ||
                evaluated > static_cast<double>(std::numeric_limits<int>::max())) {
                ofLogError() << "integer expression result is outside int32 range";
                continue;
            }
            values[i] = static_cast<int>(evaluated);
        }
    }
}

template <typename T>
void Variable<T>::afterDraw() {
}

template<>
void Variable<float>::afterDraw() {
//    if (type == "tidal") {
//        for (int i=0; i<values.size(); i++) {
//            float val = values[i] - 1.f/8.f;
//            if (val < 0.f) {
//                val = 0.f;
//            }
//            values[i] = val;
//        }
//    }
}

template<typename T>
BufData Variable<T>::asBufferData() {
    GLsizeiptr size = values.size() * sizeof(T);
    const void* ptr = values.empty() ? nullptr : static_cast<const void*>(values.data());
    return {size, ptr};
}

template<>
BufData Variable<ofxExprNode>::asBufferData() {
    GLsizeiptr size = values.size() * sizeof(glm::mat4);
    matrices.resize(values.size());
    for (int i=0; i<values.size(); i++) {
        matrices[i] = values[i].getLocalTransformMatrix();
    }
    const void* ptr = matrices.empty() ? nullptr : static_cast<const void*>(matrices.data());
    return {size, ptr};
}

template<typename T>
ofBufferObject Variable<T>::asBufferObject() {
    ofBufferObject buf;
    BufData bufData = asBufferData();
    if (bufData.bytes > 0 && bufData.data != nullptr) {
        buf.allocate(bufData.bytes, bufData.data, GL_STREAM_DRAW);
    }
    return buf;
}

template class Variable<float>;
template class Variable<int>;
template class Variable<glm::vec2>;
template class Variable<glm::vec3>;
template class Variable<glm::mat4>;
template class Variable<ofFloatColor>;
template class Variable<ofxExprNode>;
