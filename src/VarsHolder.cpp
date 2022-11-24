#include "VarsHolder.h"
#include "Config.h"
#include "VariablePool.h"

Variable* VarsHolder::getVariable(string name) {
    if (vars.find(name) != vars.end()) {
        return vars[name];
    }
    return NULL;
}

Variable* VarsHolder::getVariableOfType(string type) {
    for (map<string, Variable*>::iterator it=vars.begin(); it!=vars.end(); ++it) {
        if (it->second->type == type) {
            return it->second;
        }
    }
    return NULL;
}

bool VarsHolder::hasVar(string name) {
    return getVariable(name) != NULL;
}

float VarsHolder::getVar(string name, int idx) {
    Variable* var = getVariable(name);
    if (var != NULL) {
        return var->values[idx];
    }
    return 0;
}

vector<float> VarsHolder::getVarVec(string name) {
    Variable* var = getVariable(name);
    if (var != NULL) {
        return var->values;
    }
    return vector<float>();
}

glm::vec3 VarsHolder::getVarVec3(string name, glm::vec3 defVal) {
    Variable* var = getVariable(name);
    if (var != NULL) {
        return glm::vec3(var->values[0], var->values[1], var->values[2]);
    }
    return defVal;
}

ofFloatColor VarsHolder::getVarColor(string name) {
    Variable* var = getVariable(name);
    if (var != NULL) {
        return ofFloatColor(var->values[0], var->values[1], var->values[2]);
    }
    return ofFloatColor();
}

void VarsHolder::setVar(string name, Variable* var) {
    auto it = DataSourceMap.find(var->type);
    if (it != DataSourceMap.end() || var->type.substr(0, 5) == "tidal") {
        vars[name] = var;
    }
    else {
        ofLog() << "invalid var " + name + " type: " + var->type;
    }
}

void VarsHolder::setVar(string name, float value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->init(value);
    setVar(name, var);
}

void VarsHolder::setVar(string name, vector<float> value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->init(value);
    setVar(name, var);
}

void VarsHolder::setVar(string name, glm::vec3 value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->isVec3 = true;
    var->init(value);
    setVar(name, var);
}

void VarsHolder::setVar(string name, ofFloatColor value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->isColor = true;
    var->init(value);
    setVar(name, var);
}

void VarsHolder::setVar(string name, const ofxOscMessage& value) {
    Variable* var = VariablePool::getOrCreate(this, name);
    var->init(value);
    setVar(name, var);
}
