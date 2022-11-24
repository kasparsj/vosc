#include "VarsHolder.h"
#include "VariablePool.h"

Variable* VarsHolder::getVariable(string name) {
    if (vars.find(name) != vars.end()) {
        return vars[name];
    }
    return NULL;
}

bool VarsHolder::hasVar(string name) {
    return getVariable(name) != NULL;
}

float VarsHolder::getVar(string name, int idx) {
    Variable* var = getVariable(name);
    if (var != NULL) {
        return var->get(idx);
    }
    return 0;
}

vector<float> VarsHolder::getVarVec(string name) {
    Variable* var = getVariable(name);
    if (var != NULL) {
        return var->getVec();
    }
    return vector<float>();
}

glm::vec3 VarsHolder::getVarVec3(string name, glm::vec3 defVal) {
    Variable* var = getVariable(name);
    if (var != NULL) {
        return glm::vec3(var->get(0), var->get(1), var->get(2));
    }
    return defVal;
}

ofFloatColor VarsHolder::getVarColor(string name) {
    Variable* var = getVariable(name);
    if (var != NULL) {
        return var->getColor();
    }
    return ofFloatColor();
}

void VarsHolder::setVar(string name, Variable* var) {
    vars[name] = var;
}

void VarsHolder::setVar(string name, float value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->set(value);
    setVar(name, var);
}

void VarsHolder::setVar(string name, vector<float> value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->set(value);
    setVar(name, var);
}

void VarsHolder::setVar(string name, glm::vec3 value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->isVec3 = true;
    var->set(value);
    setVar(name, var);
}

void VarsHolder::setVar(string name, ofFloatColor value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->isColor = true;
    var->set(value);
    setVar(name, var);
}

void VarsHolder::setVar(string name, const ofxOscMessage& value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->set(value);
    setVar(name, var);
}
