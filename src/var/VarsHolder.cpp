#include "VarsHolder.h"
#include "VariablePool.h"

const shared_ptr<BaseVar>& VarsHolder::getVariable(const string& name) const {
    return vars.at(name);
}

bool VarsHolder::hasVar(const string& name) const {
    return vars.find(name) != vars.end();
}

float VarsHolder::getVar(const string& name, int idx) const {
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get(idx);
        }
    }
    return 0;
}

float VarsHolder::getVarPercent(const string& name, int idx) const {
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            float val = var->get(idx);
            return (val > 1.f ? val / 100.f : val);
        }
    }
    return 0;
}

bool VarsHolder::getVarBool(const string& name, int idx) const {
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get(idx);
        }
    }
    return false;
}

vector<float> VarsHolder::getVarVec(const string& name) const {
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            return var->getVec();
        }
    }
    return vector<float>();
}

glm::vec3 VarsHolder::getVarVec3(const string& name, glm::vec3 defVal) const {
    if (hasVar(name)) {
        const Variable<glm::vec3>* var = dynamic_cast<const Variable<glm::vec3>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get();
        }
    }
    return defVal;
}

ofFloatColor VarsHolder::getVarColor(const string& name) const {
    if (hasVar(name)) {
        const Variable<ofFloatColor>* var = dynamic_cast<const Variable<ofFloatColor>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get();
        }
    }
    return ofFloatColor();
}

template <typename T>
Variable<T>* VarsHolder::setVar(const string& name, T value) {
    Variable<T>* var = VariablePool::getOrCreate<T>(name, this);
    var->set(value);
    vars[name] = VariablePool::get(name, this);
    return var;
}

Variable<float>* VarsHolder::setVar(const string& name, bool value) {
    return setVar(name, (float) value);
}

template <typename T>
Variable<T>* VarsHolder::setVar(const string& name, vector<T> value) {
    Variable<T>* var = VariablePool::getOrCreate<T>(name, this);
    var->set(value);
    vars[name] = VariablePool::get(name, this);
    return var;
}

void VarsHolder::setVar(const string& name, const ofxOscMessage& m, int idx) {
    shared_ptr<BaseVar>& var = VariablePool::createOrUpdate(name, m, idx, this);
    vars[name] = var;
    return var;
}

template Variable<float>* VarsHolder::setVar(const string& name, float value);
template Variable<glm::vec3>* VarsHolder::setVar(const string& name, glm::vec3 value);
template Variable<glm::mat4>* VarsHolder::setVar(const string& name, glm::mat4 value);
template Variable<ofFloatColor>* VarsHolder::setVar(const string& name, ofFloatColor value);

template Variable<float>* VarsHolder::setVar(const string& name, vector<float> value);
template Variable<glm::vec3>* VarsHolder::setVar(const string& name, vector<glm::vec3> value);
template Variable<glm::mat4>* VarsHolder::setVar(const string& name, vector<glm::mat4> value);
template Variable<ofFloatColor>* VarsHolder::setVar(const string& name, vector<ofFloatColor> value);
