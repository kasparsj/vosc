#include "VariablePool.h"
#include "Texture.h"
#include "Shader.h"
#include "Layer.h"
#include "VarsHolder.h"
#include "ColorWheelSchemes.h"

map<string, BaseVar*> VariablePool::sharedPool;
map<int, map<string, BaseVar*>> VariablePool::texturePool;
map<int, map<string, BaseVar*>> VariablePool::shaderPool;
map<int, map<string, BaseVar*>> VariablePool::layerPool;

bool VariablePool::hasShared(string name) {
    return sharedPool.find(name) != sharedPool.end();
}

BaseVar* VariablePool::getShared(string name) {
    return sharedPool.at(name);
}

BaseVar* VariablePool::getOrCreateShared(string name, const ofxOscMessage& m, int idx) {
    if (!hasShared(name)) {
        sharedPool[name] = create(m, idx);
    }
    return sharedPool.at(name);
}

template<typename T>
Variable<T>* VariablePool::getOrCreateShared(string name) {
    if (!hasShared(name)) {
        sharedPool[name] = new Variable<T>();
    }
    return static_cast<Variable<T>*>(sharedPool.at(name));
}

BaseVar* VariablePool::create(const ofxOscMessage& m, int idx) {
    string command = m.getAddress();
    if (command.length() >= 4 && command.substr(command.length()-4) == "/var") {
        switch (m.getArgType(idx)) {
            case OFXOSC_TYPE_STRING: {
                Variable<float>* var = new Variable<float>();
                var->set(m, idx);
                return var;
            }
            case OFXOSC_TYPE_FLOAT:
            case OFXOSC_TYPE_INT32:
            case OFXOSC_TYPE_TRUE:
            case OFXOSC_TYPE_FALSE: {
                Variable<float>* var = new Variable<float>();
                var->set(m, idx);
                return var;
            }
            case OFXOSC_TYPE_BLOB:
            default:
                ofBuffer buf = m.getArgAsBlob(idx);
                if (buf.size() == 3) {
                    Variable<glm::vec3>* var = new Variable<glm::vec3>();
                    var->set(m, idx);
                    return var;
                }
                else if (buf.size() == 4) {
                    Variable<ofFloatColor>* var = new Variable<ofFloatColor>();
                    var->set(m, idx);
                    return var;
                }
                else if (buf.size() == 16) {
                    Variable<glm::mat4>* var = new Variable<glm::mat4>();
                    var->set(m, idx);
                    return var;
                }
                else {
                    ofLogError() << "BLOB vars not implemented!";
                }
                break;
        }
    }
    else if (command.length() >= 11 && command.substr(command.length()-11) == "/var/colors") {
        Variable<ofFloatColor>* var = new Variable<ofFloatColor>();
        string schemeName = m.getArgAsString(idx+1);
        shared_ptr<ofxColorTheory::FloatColorWheelScheme> scheme = ofxColorTheory::FloatColorWheelSchemes::get(schemeName);
        if (scheme != NULL) {
            ofBuffer buf = m.getArgAsBlob(idx);
            scheme->setPrimaryColor(Args::get().parseColor(m, idx));
            int numColors = m.getArgAsInt(idx+2);
            var->set(scheme->interpolate(numColors));
        }
        else {
            ofLogError() << "/var/colors invalid scheme name: " << schemeName;
        }
        return var;
    }
    else {
        ofLogError() << "command not recognized: " << m;
    }
}

BaseVar* VariablePool::get(VarsHolder* holder, string name) {
    map<string, BaseVar*>& pool = getPool(holder);
    return pool[name];
}

BaseVar* VariablePool::getOrCreate(string name, const ofxOscMessage& m, int idx, VarsHolder* holder) {
    map<string, BaseVar*>& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = create(m, idx);
    }
    return pool.at(name);
}

template <typename T>
Variable<T>* VariablePool::getOrCreate(string name, VarsHolder* holder) {
    map<string, BaseVar*>& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = new Variable<T>();
    }
    return static_cast<Variable<T>*>(pool.at(name));
}

map<string, BaseVar*>& VariablePool::getPool(VarsHolder* holder) {
    if (dynamic_cast<Texture*>(holder) != NULL) {
        return texturePool[holder->getId()];
    }
    else if (dynamic_cast<Shader*>(holder) != NULL) {
        return shaderPool[holder->getId()];
    }
    else if (dynamic_cast<Layer*>(holder) != NULL) {
        return layerPool[holder->getId()];
    }
    else {
        return sharedPool;
    }
}

void VariablePool::update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    for (map<string, BaseVar*>::iterator it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second->update(mics, sounds, notes);
    }
    for (map<int, map<string, BaseVar*>>::iterator it=texturePool.begin(); it!=texturePool.end(); ++it) {
        for (map<string, BaseVar*>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(mics, sounds, notes);
        }
    }
    for (map<int, map<string, BaseVar*>>::iterator it=shaderPool.begin(); it!=shaderPool.end(); ++it) {
        for (map<string, BaseVar*>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(mics, sounds, notes);
        }
    }
}

void VariablePool::cleanup(VarsHolder* holder) {
    map<string, BaseVar*>& pool = getPool(holder);
    for (map<string, BaseVar*>::iterator it2=pool.begin(); it2!=pool.end(); ++it2) {
        delete it2->second;
    }
    if (dynamic_cast<Texture*>(holder) != NULL) {
        texturePool.erase(holder->getId());
    }
    else if (dynamic_cast<Shader*>(holder) != NULL) {
        shaderPool.erase(holder->getId());
    }
    else if (dynamic_cast<Layer*>(holder) != NULL) {
        layerPool.erase(holder->getId());
    }
}

template Variable<float>* VariablePool::getOrCreateShared(string name);
template Variable<glm::vec3>* VariablePool::getOrCreateShared(string name);
template Variable<glm::mat4>* VariablePool::getOrCreateShared(string name);
template Variable<ofFloatColor>* VariablePool::getOrCreateShared(string name);

template Variable<float>* VariablePool::getOrCreate(string name, VarsHolder* holder);
template Variable<glm::vec3>* VariablePool::getOrCreate(string name, VarsHolder* holder);
template Variable<glm::mat4>* VariablePool::getOrCreate(string name, VarsHolder* holder);
template Variable<ofFloatColor>* VariablePool::getOrCreate(string name, VarsHolder* holder);
