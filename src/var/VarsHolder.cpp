#include "VarsHolder.h"
#include "VariablePool.h"

const BaseVar* VarsHolder::getVariable(string name) const {
    if (vars.find(name) != vars.end()) {
        return vars.at(name);
    }
    return NULL;
}

bool VarsHolder::hasVar(string name) const {
    return getVariable(name) != NULL;
}

float VarsHolder::getVar(string name, int idx) const {
    const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name));
    if (var != NULL) {
        return var->get(idx);
    }
    return 0;
}

bool VarsHolder::getVarBool(string name, int idx) const {
    const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name));
    if (var != NULL) {
        return var->get(idx);
    }
    return false;
}

vector<float> VarsHolder::getVarVec(string name) const {
    const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name));
    if (var != NULL) {
        return var->getVec();
    }
    return vector<float>();
}

glm::vec3 VarsHolder::getVarVec3(string name, glm::vec3 defVal) const {
    const Variable<glm::vec3>* var = dynamic_cast<const Variable<glm::vec3>*>(getVariable(name));
    if (var != NULL) {
        return var->get();
    }
    return defVal;
}

ofFloatColor VarsHolder::getVarColor(string name) const {
    const Variable<ofFloatColor>* var = dynamic_cast<const Variable<ofFloatColor>*>(getVariable(name));
    if (var != NULL) {
        return var->get();
    }
    return ofFloatColor();
}

template <typename T>
Variable<T>* VarsHolder::setVar(string name, T value) {
    Variable<T>* var = VariablePool::getOrCreate<T>(name, this);
    var->set(value);
    vars[name] = var;
    return var;
}

Variable<float>* VarsHolder::setVar(string name, bool value) {
    return setVar(name, (float) value);
}

template <typename T>
Variable<T>* VarsHolder::setVar(string name, vector<T> value) {
    Variable<T>* var = VariablePool::getOrCreate<T>(name, this);
    var->set(value);
    vars[name] = var;
    return var;
}

void VarsHolder::setVar(string name, const ofxOscMessage& value, int idx) {
    BaseVar* var = VariablePool::getOrCreate(name, value, idx, this);
    vars[name] = var;
    return var;
}

template Variable<float>* VarsHolder::setVar(string name, float value);
template Variable<glm::vec3>* VarsHolder::setVar(string name, glm::vec3 value);
template Variable<glm::mat4>* VarsHolder::setVar(string name, glm::mat4 value);
template Variable<ofFloatColor>* VarsHolder::setVar(string name, ofFloatColor value);

template Variable<float>* VarsHolder::setVar(string name, vector<float> value);
template Variable<glm::vec3>* VarsHolder::setVar(string name, vector<glm::vec3> value);
template Variable<glm::mat4>* VarsHolder::setVar(string name, vector<glm::mat4> value);
template Variable<ofFloatColor>* VarsHolder::setVar(string name, vector<ofFloatColor> value);
