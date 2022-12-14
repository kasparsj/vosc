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
    Variable<T>* var = VariablePool::getOrCreate<T>(this, name);
    var->set(value);
    vars[name] = var;
    return var;
}

Variable<float>* VarsHolder::setVar(string name, bool value) {
    return setVar(name, (float) value);
}

template <typename T>
Variable<T>* VarsHolder::setVar(string name, vector<T> value) {
    Variable<T>* var = VariablePool::getOrCreate<T>(this, name);
    var->set(value);
    vars[name] = var;
    return var;
}

void VarsHolder::setVar(string name, const ofxOscMessage& value, int idx) {
    switch (value.getArgType(idx)) {
        case OFXOSC_TYPE_STRING: {
            Variable<float>* var = VariablePool::getOrCreate<float>(this, name);
            var->set(value, idx);
            vars[name] = var;
            break;
        }
        case OFXOSC_TYPE_FLOAT:
        case OFXOSC_TYPE_INT32:
        case OFXOSC_TYPE_TRUE:
        case OFXOSC_TYPE_FALSE: {
            Variable<float>* var = VariablePool::getOrCreate<float>(this, name);
            var->set(value, idx);
            vars[name] = var;
            break;
        }
        case OFXOSC_TYPE_BLOB:
        default:
            ofBuffer buf = value.getArgAsBlob(idx);
            if (buf.size() == 3) {
                // vec3
            }
            else if (buf.size() == 4) {
                // ofFloatColor
            }
            else if (buf.size() == 16) {
                // mat4
            }
            else {
                ofLogError() << "BLOB vars not implemented!";
            }
            break;
    }
}

template Variable<float>* VarsHolder::setVar(string name, float value);
template Variable<glm::vec3>* VarsHolder::setVar(string name, glm::vec3 value);
template Variable<glm::mat4>* VarsHolder::setVar(string name, glm::mat4 value);
template Variable<ofFloatColor>* VarsHolder::setVar(string name, ofFloatColor value);

template Variable<float>* VarsHolder::setVar(string name, vector<float> value);
template Variable<glm::vec3>* VarsHolder::setVar(string name, vector<glm::vec3> value);
template Variable<glm::mat4>* VarsHolder::setVar(string name, vector<glm::mat4> value);
template Variable<ofFloatColor>* VarsHolder::setVar(string name, vector<ofFloatColor> value);
