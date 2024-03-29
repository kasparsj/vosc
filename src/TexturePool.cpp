#include "TexturePool.h"
#include "Shader.h"

map<string, shared_ptr<Texture>> TexturePool::sharedPool;
map<int, map<string, shared_ptr<Texture>>> TexturePool::shaderPool;

bool TexturePool::hasShared(string name) {
    return sharedPool.find(name) != sharedPool.end();
}

shared_ptr<Texture>& TexturePool::getShared(string name, bool create) {
    if (create && !hasShared(name)) {
        sharedPool[name] = make_shared<Texture>();
    }
    return sharedPool.at(name);
}

shared_ptr<Texture>& TexturePool::getOrCreate(string name, const VarsHolder* holder) {
    map<string, shared_ptr<Texture>>& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = make_shared<Texture>();
    }
    return pool.at(name);
}

map<string, shared_ptr<Texture>>& TexturePool::getPool(const VarsHolder* holder) {
    if (holder == NULL) {
        return sharedPool;
    }
    else if (dynamic_cast<const Shader*>(holder) != NULL) {
        return shaderPool[holder->getId()];
    }
    throw "VariablePool::getPool incompatible holder: " + ofToString(holder);
}

void TexturePool::update(const vector<TidalNote> &notes) {
    for (map<string, shared_ptr<Texture>>::iterator it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second->update(notes);
    }
    for (map<int, map<string, shared_ptr<Texture>>>::iterator it=shaderPool.begin(); it!=shaderPool.end(); ++it) {
        for (map<string, shared_ptr<Texture>>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(notes);
        }
    }
}

void TexturePool::clean(int shaderId) {
    shaderPool[shaderId].clear();
    shaderPool.erase(shaderId);
}
