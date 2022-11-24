#include "VariablePool.h"
#include "Texture.h"
#include "Shader.h"
#include "Layer.h"

map<string, Variable*> VariablePool::sharedPool;
map<int, map<string, Variable*>> VariablePool::texturePool;
map<int, map<string, Variable*>> VariablePool::shaderPool;
map<int, map<string, Variable*>> VariablePool::layerPool;

bool VariablePool::hasShared(string name) {
    return sharedPool.find(name) != sharedPool.end();
}

Variable* VariablePool::getShared(string name, bool create) {
    if (create && !hasShared(name)) {
        sharedPool[name] = new Variable();
    }
    return sharedPool[name];
}

Variable* VariablePool::get(VarsHolder* holder, string name) {
    map<string, Variable*>& pool = getPool(holder);
    return pool[name];
}

Variable* VariablePool::getOrCreate(VarsHolder* holder, string name) {
    map<string, Variable*>& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = new Variable();
    }
    return pool[name];
}

map<string, Variable*>& VariablePool::getPool(VarsHolder* holder) {
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

void VariablePool::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    for (map<string, Variable*>::iterator it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second->update(sounds, notes);
    }
    for (map<int, map<string, Variable*>>::iterator it=texturePool.begin(); it!=texturePool.end(); ++it) {
        for (map<string, Variable*>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(sounds, notes);
        }
    }
    for (map<int, map<string, Variable*>>::iterator it=shaderPool.begin(); it!=shaderPool.end(); ++it) {
        for (map<string, Variable*>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(sounds, notes);
        }
    }
    for (map<int, map<string, Variable*>>::iterator it=layerPool.begin(); it!=layerPool.end(); ++it) {
        for (map<string, Variable*>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(sounds, notes);
        }
    }
}

void VariablePool::cleanup(VarsHolder* holder) {
    map<string, Variable*>& pool = getPool(holder);
    for (map<string, Variable*>::iterator it2=pool.begin(); it2!=pool.end(); ++it2) {
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
