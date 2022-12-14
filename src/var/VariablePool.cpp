#include "VariablePool.h"
#include "Texture.h"
#include "Shader.h"
#include "Layer.h"
#include "Variable.h"
#include "VarsHolder.h"

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

template <typename T>
Variable<T>* VariablePool::getOrCreateShared(string name) {
    if (!hasShared(name)) {
        sharedPool[name] = new Variable<T>();
    }
    return static_cast<Variable<T>*>(sharedPool.at(name));
}

BaseVar* VariablePool::get(VarsHolder* holder, string name) {
    map<string, BaseVar*>& pool = getPool(holder);
    return pool[name];
}

template <typename T>
Variable<T>* VariablePool::getOrCreate(VarsHolder* holder, string name) {
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

template Variable<float>* VariablePool::getOrCreate(VarsHolder* holder, string name);
template Variable<glm::vec3>* VariablePool::getOrCreate(VarsHolder* holder, string name);
template Variable<glm::mat4>* VariablePool::getOrCreate(VarsHolder* holder, string name);
template Variable<ofFloatColor>* VariablePool::getOrCreate(VarsHolder* holder, string name);
