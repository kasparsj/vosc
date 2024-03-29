#include "VarsHolder.h"
#include "VariablePool.h"

const shared_ptr<BaseVar>& VarsHolder::getVariable(const string& name) const {
    return vars.at(name);
}

bool VarsHolder::hasVar(const string& name) const {
    return vars.find(name) != vars.end();
}

float VarsHolder::getVar(const string& name, int idx) const {
    // todo: should check existance and type before
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get(idx);
        }
    }
    return 0;
}

float VarsHolder::getVarPercent(const string& name, int idx) const {
    // todo: should check existance and type before
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
    // todo: should check existance and type before
    if (hasVar(name)) {
        const Variable<float>* var = dynamic_cast<const Variable<float>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get(idx);
        }
    }
    return false;
}

const vector<float>& VarsHolder::getVarVec(const string& name) const {
    return static_cast<const Variable<float>*>(getVariable(name).get())->getVec();
}

vector<float>& VarsHolder::getVarVec(const string& name) {
    return static_cast<Variable<float>*>(getVariable(name).get())->getVec();
}

glm::vec3 VarsHolder::getVarVec3(const string& name, glm::vec3 defVal) const {
    // todo: should check existance and type before
    if (hasVar(name)) {
        const Variable<glm::vec3>* var = dynamic_cast<const Variable<glm::vec3>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get();
        }
    }
    return defVal;
}

ofFloatColor VarsHolder::getVarColor(const string& name) const {
    // todo: should check existance and type before
    if (hasVar(name)) {
        const Variable<ofFloatColor>* var = dynamic_cast<const Variable<ofFloatColor>*>(getVariable(name).get());
        if (var != NULL) {
            return var->get();
        }
    }
    return ofFloatColor();
}

template <typename T>
const shared_ptr<Variable<T>>& VarsHolder::setVar(const string& name, T value) {
    const auto& var = VariablePool::getOrCreate<T>(name, this);
    var->set(value);
    vars[name] = VariablePool::get(name, this);
    return var;
}

const shared_ptr<Variable<float>>& VarsHolder::setVar(const string& name, bool value) {
    return setVar(name, (float) value);
}

template <typename T>
const shared_ptr<Variable<T>>& VarsHolder::setVar(const string& name, vector<T> value) {
    const auto& var = VariablePool::getOrCreate<T>(name, this);
    var->set(value);
    vars[name] = VariablePool::get(name, this);
    return var;
}

void VarsHolder::setVar(const string& name, const ofxOscMessage& m, int idx) {
    auto& var = VariablePool::createOrUpdate(name, m, idx, this);
    vars[name] = var;
    return var;
}

template const shared_ptr<Variable<float>>& VarsHolder::setVar(const string& name, float value);
template const shared_ptr<Variable<glm::vec3>>& VarsHolder::setVar(const string& name, glm::vec3 value);
template const shared_ptr<Variable<glm::mat4>>& VarsHolder::setVar(const string& name, glm::mat4 value);
template const shared_ptr<Variable<ofFloatColor>>& VarsHolder::setVar(const string& name, ofFloatColor value);

template const shared_ptr<Variable<float>>& VarsHolder::setVar(const string& name, vector<float> value);
template const shared_ptr<Variable<glm::vec3>>& VarsHolder::setVar(const string& name, vector<glm::vec3> value);
template const shared_ptr<Variable<glm::mat4>>& VarsHolder::setVar(const string& name, vector<glm::mat4> value);
template const shared_ptr<Variable<ofFloatColor>>& VarsHolder::setVar(const string& name, vector<ofFloatColor> value);
