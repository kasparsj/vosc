#include "ShaderPool.h"
#include "ofxTidalCycles.h"
#include "../ResourceRegistry.hpp"

namespace {
map<string, shared_ptr<Shader>>& emptyPool() {
    static map<string, shared_ptr<Shader>> pool;
    return pool;
}
}

bool ShaderPool::hasShared(string name) {
    ResourceRegistry* registry = ResourceRegistry::current();
    return registry != nullptr && registry->shaderSharedPool.find(name) != registry->shaderSharedPool.end();
}

shared_ptr<Shader> ShaderPool::getShared(string name, bool create) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "ShaderPool::getShared called without active ResourceRegistry";
        return nullptr;
    }
    if (create && !hasShared(name)) {
        registry->shaderSharedPool[name] = make_shared<Shader>();
    }
    auto it = registry->shaderSharedPool.find(name);
    if (it == registry->shaderSharedPool.end()) {
        return nullptr;
    }
    return it->second;
}

shared_ptr<Shader> ShaderPool::getOrCreate(string name) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "ShaderPool::getOrCreate called without active ResourceRegistry";
        return nullptr;
    }
    if (registry->shaderSharedPool.find(name) == registry->shaderSharedPool.end()) {
        registry->shaderSharedPool[name] = make_shared<Shader>();
    }
    return registry->shaderSharedPool.at(name);
}

map<string, shared_ptr<Shader>>& ShaderPool::getPool() {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "ShaderPool::getPool called without active ResourceRegistry";
        return emptyPool();
    }
    return registry->shaderSharedPool;
}

void ShaderPool::update(const vector<TidalNote> &notes) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        return;
    }
    for (map<string, shared_ptr<Shader>>::iterator it=registry->shaderSharedPool.begin(); it!=registry->shaderSharedPool.end(); ++it) {
        it->second->update(notes);
    }
}

void ShaderPool::clean() {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        return;
    }
    registry->shaderSharedPool.clear();
}
