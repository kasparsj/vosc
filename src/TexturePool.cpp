#include "TexturePool.h"
#include "shader/Shader.h"
#include "ResourceRegistry.hpp"

namespace {
map<string, shared_ptr<Texture>>& emptyPool() {
    static map<string, shared_ptr<Texture>> pool;
    return pool;
}
}

bool TexturePool::hasShared(string name) {
    ResourceRegistry* registry = ResourceRegistry::current();
    return registry != nullptr && registry->textureSharedPool.find(name) != registry->textureSharedPool.end();
}

shared_ptr<Texture> TexturePool::getShared(string name, bool create) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "TexturePool::getShared called without active ResourceRegistry";
        return nullptr;
    }
    if (create && !hasShared(name)) {
        registry->textureSharedPool[name] = make_shared<Texture>();
    }
    auto it = registry->textureSharedPool.find(name);
    if (it == registry->textureSharedPool.end()) {
        return nullptr;
    }
    return it->second;
}

shared_ptr<Texture> TexturePool::getOrCreate(string name, const VarsHolder* holder) {
    map<string, shared_ptr<Texture>>& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = make_shared<Texture>();
    }
    return pool.at(name);
}

map<string, shared_ptr<Texture>>& TexturePool::getPool(const VarsHolder* holder) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "TexturePool::getPool called without active ResourceRegistry";
        return emptyPool();
    }
    if (holder == nullptr) {
        return registry->textureSharedPool;
    }
    else if (dynamic_cast<const Shader*>(holder) != nullptr) {
        return registry->textureShaderPool[holder->getId()];
    }
    throw "VariablePool::getPool incompatible holder: " + ofToString(holder);
}

void TexturePool::update(const vector<TidalNote> &notes) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        return;
    }

    for (map<string, shared_ptr<Texture>>::iterator it=registry->textureSharedPool.begin(); it!=registry->textureSharedPool.end(); ++it) {
        it->second->update(notes);
    }
    for (map<int, map<string, shared_ptr<Texture>>>::iterator it=registry->textureShaderPool.begin(); it!=registry->textureShaderPool.end(); ++it) {
        for (map<string, shared_ptr<Texture>>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(notes);
        }
    }
}

void TexturePool::clean(int shaderId) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        return;
    }
    registry->textureShaderPool[shaderId].clear();
    registry->textureShaderPool.erase(shaderId);
}
