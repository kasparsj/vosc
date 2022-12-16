#include "VarsHolder.h"
#include "VariablePool.h"

const shared_ptr<BaseVar>& VarsHolder::getVariable(string name) const {
    return vars.at(name);
}

bool VarsHolder::hasVar(string name) const {
    return vars.find(name) != vars.end();
}

float VarsHolder::getVar(string name, int idx) const {
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get(idx);
        }
    }
    return 0;
}

bool VarsHolder::getVarBool(string name, int idx) const {
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get(idx);
        }
    }
    return false;
}

vector<float> VarsHolder::getVarVec(string name) const {
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            return var->getVec();
        }
    }
    return vector<float>();
}

glm::vec3 VarsHolder::getVarVec3(string name, glm::vec3 defVal) const {
    if (hasVar(name)) {
        const Variable<glm::vec3>* var = dynamic_cast<const Variable<glm::vec3>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get();
        }
    }
    return defVal;
}

ofFloatColor VarsHolder::getVarColor(string name) const {
    if (hasVar(name)) {
        const Variable<ofFloatColor>* var = dynamic_cast<const Variable<ofFloatColor>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get();
        }
    }
    return ofFloatColor();
}

template <typename T>
Variable<T>* VarsHolder::setVar(string name, T value) {
    Variable<T>* var = VariablePool::getOrCreate<T>(name, this);
    var->set(value);
    vars[name] = VariablePool::get(name, this);
    return var;
}

Variable<float>* VarsHolder::setVar(string name, bool value) {
    return setVar(name, (float) value);
}

template <typename T>
Variable<T>* VarsHolder::setVar(string name, vector<T> value) {
    Variable<T>* var = VariablePool::getOrCreate<T>(name, this);
    var->set(value);
    vars[name] = VariablePool::get(name, this);
    return var;
}

void VarsHolder::setVar(string name, const ofxOscMessage& m, int idx) {
    shared_ptr<BaseVar>& var = VariablePool::createOrUpdate(name, m, idx, this);
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
