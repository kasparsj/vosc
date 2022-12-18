#include "VariablePool.h"
#include "Texture.h"
#include "Shader.h"
#include "Layer.h"
#include "VarsHolder.h"
#include "ColorWheelSchemes.h"
#include "ofxExpr.hpp"
#include "Args.h"

map<string, shared_ptr<BaseVar>> VariablePool::sharedPool;
map<int, map<string, shared_ptr<BaseVar>>> VariablePool::holderPool;

bool VariablePool::hasShared(const string& name) {
    return sharedPool.find(name) != sharedPool.end();
}

shared_ptr<BaseVar>& VariablePool::getShared(const string& name) {
    return sharedPool.at(name);
}

shared_ptr<BaseVar>& VariablePool::createOrUpdateShared(const string& name, const ofxOscMessage& m, int idx) {
    return createOrUpdateShared(name, m.getAddress(), m, idx);
}

shared_ptr<BaseVar>& VariablePool::createOrUpdateShared(const string& name, const string& command, const ofxOscMessage& m, int idx) {
    if (hasShared(name)) {
        update(sharedPool.at(name), command, m, idx);
    }
    else {
        sharedPool[name] = create(command, m, idx);
    }
    return sharedPool.at(name);
}

template<typename T>
Variable<T>* VariablePool::createOrUpdateShared(const string& name, T value) {
    if (!hasShared(name)) {
        sharedPool[name] = make_shared<Variable<T>>();
    }
    const shared_ptr<Variable<T>> var = static_pointer_cast<Variable<T>>(sharedPool.at(name));
    var->set(value);
    return var.get();
}

shared_ptr<BaseVar> VariablePool::create(const ofxOscMessage& m, int idx) {
    string command = m.getAddress();
    return create(command, m, idx);
}

shared_ptr<BaseVar> VariablePool::create(const string& command, const ofxOscMessage& m, int idx) {
    if (command.length() >= 4 && command.substr(command.length()-4) == "/var") {
        ofxOscArgType type = m.getArgType(idx);
        switch (type) {
            case OFXOSC_TYPE_STRING: {
                string str = m.getArgAsString(idx);
                if (Args::isJSON(str)) {
                    auto json = ofJson::parse(str);
                    return create(m, idx, json.size());
                }
                else {
                    shared_ptr<Variable<float>> var = make_shared<Variable<float>>();
                    var->set(m, idx);
                    return var;
                }
            }
            case OFXOSC_TYPE_FLOAT:
            case OFXOSC_TYPE_INT32:
            case OFXOSC_TYPE_TRUE:
            case OFXOSC_TYPE_FALSE: {
                shared_ptr<Variable<float>> var = make_shared<Variable<float>>();
                var->set(m, idx);
                return var;
            }
            case OFXOSC_TYPE_BLOB: {
                ofBuffer buf = m.getArgAsBlob(idx);
                return create(m, idx, buf.size());
            }
            default:
                ofLogError() << "VariablePool::create type not implemented: " << type << m << idx;
                return NULL;
        }
    }
    else if (command.length() >= 11 && command.substr(command.length()-11) == "/var/colors") {
        shared_ptr<Variable<ofFloatColor>> var = make_shared<Variable<ofFloatColor>>();
        var->set(m, idx);
        return var;
    }
    else if (command.length() >= 18 && command.substr(command.length()-18) == "/var/colors/scheme") {
        shared_ptr<Variable<ofFloatColor>> var = make_shared<Variable<ofFloatColor>>();
        updateColorsScheme(var, m, idx);
        return var;
    }
    else if (command.length() >= 10 && command.substr(command.length()-10) == "/var/nodes") {
        shared_ptr<Variable<ofxExprNode>> var = make_shared<Variable<ofxExprNode>>();
        var->set(m, idx);
        return var;
    }
    ofLogError() << "VariablePool::create command not recognized: " << m;
    return NULL;
}

shared_ptr<BaseVar> VariablePool::create(const ofxOscMessage& m, int idx, size_t size) {
    if (size == 3) {
        Variable<glm::vec3>* var = new Variable<glm::vec3>();
        var->set(m, idx);
        return shared_ptr<BaseVar>(var);
    }
    else if (size == 4) {
        Variable<ofFloatColor>* var = new Variable<ofFloatColor>();
        var->set(m, idx);
        return shared_ptr<BaseVar>(var);
    }
    else if (size == 16) {
        Variable<glm::mat4>* var = new Variable<glm::mat4>();
        var->set(m, idx);
        return shared_ptr<BaseVar>(var);
    }
    ofLogError() << "VariablePool::create not implemented for size: " << size;
    return NULL;
}

void VariablePool::update(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx) {
    const string& command = m.getAddress();
    update(var, command, m, idx);
}

void VariablePool::update(shared_ptr<BaseVar>& var, const string& command, const ofxOscMessage& m, int idx) {
    if (command.length() >= 4 && command.substr(command.length()-4) == "/var") {
        ofxOscArgType type = m.getArgType(idx);
        switch (type) {
            case OFXOSC_TYPE_STRING: {
                string str = m.getArgAsString(idx);
                if (Args::isJSON(str)) {
                    auto json = ofJson::parse(str);
                    update(var, m, idx, json.size());
                    return;
                }
                break;
            }
            case OFXOSC_TYPE_BLOB: {
                ofBuffer buf = m.getArgAsBlob(idx);
                update(var, m, idx, buf.size());
                return;
            }
        }
        Variable<float>* var1 = dynamic_cast<Variable<float>*>(var.get());
        if (var1 != NULL) {
            var1->set(m, idx);
            return;
        }
        Variable<ofFloatColor>* var2 = dynamic_cast<Variable<ofFloatColor>*>(var.get());
        if (var2 != NULL) {
            var2->set(m, idx);
            return;
        }
        ofLogError() << "VariablePool::update type not implemented: " << type << m << idx;
    }
    else if (command.length() >= 11 && command.substr(command.length()-11) == "/var/colors") {
        static_cast<Variable<ofFloatColor>*>(var.get())->set(m, idx);
    }
    else if (command.length() >= 18 && command.substr(command.length()-18) == "/var/colors/scheme") {
        updateColorsScheme(var, m, idx);
    }
    else if (command.length() >= 10 && command.substr(command.length()-10) == "/var/nodes") {
        static_cast<Variable<ofxExprNode>*>(var.get())->set(m, idx);
    }
}

void VariablePool::update(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx, size_t size) {
    if (size == 3) {
        shared_ptr<Variable<glm::vec3>> var1 = dynamic_pointer_cast<Variable<glm::vec3>>(var);
        if (var1 != NULL) {
            var1->set(m, idx);
            return;
        }
        shared_ptr<Variable<ofFloatColor>> var2 = dynamic_pointer_cast<Variable<ofFloatColor>>(var);
        if (var2 != NULL) {
            var2->set(m, idx);
            return;
        }
    }
    else if (size == 4) {
        shared_ptr<Variable<ofFloatColor>> var = dynamic_pointer_cast<Variable<ofFloatColor>>(var);
        if (var != NULL) {
            var->set(m, idx);
            return;
        }
    }
    else if (size == 16) {
        shared_ptr<Variable<glm::mat4>> var = static_pointer_cast<Variable<glm::mat4>>(var);
        if (var != NULL) {
            var->set(m, idx);
            return;
        }
    }
    ofLogError() << "VariablePool::create not implemented for size: " << size;
}

void VariablePool::updateColorsScheme(const shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx) {
    string schemeName = m.getArgAsString(idx);
    ofFloatColor primaryColor = Args::parse<ofFloatColor>(m, idx+1);
    shared_ptr<ofxColorTheory::FloatColorWheelScheme> scheme = ofxColorTheory::FloatColorWheelSchemes::get(schemeName);
    if (scheme != NULL) {
        scheme->setPrimaryColor(primaryColor);
        int numColors = m.getNumArgs() > (idx+2) ? m.getArgAsInt(idx+2) : 1;
        static_cast<Variable<ofFloatColor>*>(var.get())->set(scheme->interpolate(numColors));
    }
    else {
        ofLogError() << "/var/colors/scheme invalid scheme name: " << schemeName;
    }

}

shared_ptr<BaseVar>& VariablePool::get(const string& name, const VarsHolder* holder) {
    map<string, shared_ptr<BaseVar>>& pool = getPool(holder);
    return pool.at(name);
}

shared_ptr<BaseVar>& VariablePool::createOrUpdate(const string& name, const ofxOscMessage& m, int idx, const VarsHolder* holder) {
    map<string, shared_ptr<BaseVar>>& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = create("/var", m, idx);
    }
    else {
        update(pool.at(name), "/var", m, idx);
    }
    return pool.at(name);
}

template <typename T>
Variable<T>* VariablePool::getOrCreate(const string& name, const VarsHolder* holder) {
    map<string, shared_ptr<BaseVar>>& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = make_shared<Variable<T>>();
    }
    return static_cast<Variable<T>*>(pool.at(name).get());
}

map<string, shared_ptr<BaseVar>>& VariablePool::getPool(const VarsHolder* holder) {
    if (holder == NULL) {
        return sharedPool;
    }
    return holderPool[holder->getId()];
}

void VariablePool::update(const vector<OSCInput> &inputs, const vector<TidalNote> &notes) {
    for (map<string, shared_ptr<BaseVar>>::iterator it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second->update();
    }
    for (map<int, map<string, shared_ptr<BaseVar>>>::iterator it=holderPool.begin(); it!=holderPool.end(); ++it) {
        for (map<string, shared_ptr<BaseVar>>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update();
        }
    }
}

void VariablePool::cleanup(const VarsHolder* holder) {
    map<string, shared_ptr<BaseVar>>& pool = getPool(holder);
    pool.clear();
    holderPool.erase(holder->getId());
}

template Variable<float>* VariablePool::createOrUpdateShared(const string& name, float value);
template Variable<glm::vec3>* VariablePool::createOrUpdateShared(const string& name, glm::vec3 value);
template Variable<glm::mat4>* VariablePool::createOrUpdateShared(const string& name, glm::mat4 value);
template Variable<ofFloatColor>* VariablePool::createOrUpdateShared(const string& name, ofFloatColor value);

template Variable<float>* VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template Variable<glm::vec3>* VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template Variable<glm::mat4>* VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template Variable<ofFloatColor>* VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
