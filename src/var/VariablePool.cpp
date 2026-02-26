#include "VariablePool.h"
#include "Texture.h"
#include "shader/Shader.h"
#include "Layer.h"
#include "VarsHolder.h"
#include "ofxExpr.hpp"
#include "../ResourceRegistry.hpp"

namespace {
map<string, shared_ptr<BaseVar>>& emptyPool() {
    static map<string, shared_ptr<BaseVar>> pool;
    return pool;
}
}

bool VariablePool::hasShared(const string& name) {
    ResourceRegistry* registry = ResourceRegistry::current();
    return registry != nullptr && registry->variableSharedPool.find(name) != registry->variableSharedPool.end();
}

shared_ptr<BaseVar> VariablePool::getShared(const string& name) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "VariablePool::getShared called without active ResourceRegistry";
        return nullptr;
    }
    auto it = registry->variableSharedPool.find(name);
    if (it == registry->variableSharedPool.end()) {
        return nullptr;
    }
    return it->second;
}

shared_ptr<BaseVar> VariablePool::createOrUpdateShared(const string& name, const ofxOscMessage& m, int idx) {
    return createOrUpdateShared(name, m.getAddress(), m, idx);
}

shared_ptr<BaseVar> VariablePool::createOrUpdateShared(const string& name, const string& command, const ofxOscMessage& m, int idx) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "VariablePool::createOrUpdateShared called without active ResourceRegistry";
        return nullptr;
    }
    if (hasShared(name)) {
        BaseVar::update(registry->variableSharedPool.at(name), command, m, idx);
    }
    else {
        registry->variableSharedPool[name] = BaseVar::create(command, m, idx);
    }
    auto it = registry->variableSharedPool.find(name);
    if (it == registry->variableSharedPool.end()) {
        return nullptr;
    }
    return it->second;
}

template<typename T>
const shared_ptr<Variable<T>> VariablePool::createOrUpdateShared(const string& name, T value) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "VariablePool::createOrUpdateShared called without active ResourceRegistry";
        return nullptr;
    }
    if (!hasShared(name)) {
        registry->variableSharedPool[name] = make_shared<Variable<T>>();
    }
    auto var = std::static_pointer_cast<Variable<T>>(registry->variableSharedPool.at(name));
    var->set(value);
    return var;
}

shared_ptr<BaseVar> VariablePool::get(const string& name, const VarsHolder* holder) {
    map<string, shared_ptr<BaseVar>>& pool = getPool(holder);
    return pool.at(name);
}

shared_ptr<BaseVar> VariablePool::createOrUpdate(const string& name, const ofxOscMessage& m, int idx, const VarsHolder* holder) {
    auto& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = BaseVar::createVar(m, idx);
    }
    else {
        BaseVar::updateVar(pool.at(name), m, idx);
    }
    return pool.at(name);
}

template <typename T>
const shared_ptr<Variable<T>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder) {
    auto& pool = getPool(holder);
    if (pool.find(name) == pool.end()) {
        pool[name] = make_shared<Variable<T>>();
    }
    return std::static_pointer_cast<Variable<T>>(pool.at(name));
}

map<string, shared_ptr<BaseVar>>& VariablePool::getPool(const VarsHolder* holder) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "VariablePool::getPool called without active ResourceRegistry";
        return emptyPool();
    }
    if (holder == nullptr) {
        return registry->variableSharedPool;
    }
    return registry->variableHolderPool[holder->getId()];
}

void VariablePool::update(const vector<TidalNote> &notes) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        return;
    }
    for (auto it=registry->variableSharedPool.begin(); it!=registry->variableSharedPool.end(); ++it) {
        it->second->update();
    }
    for (auto it=registry->variableHolderPool.begin(); it!=registry->variableHolderPool.end(); ++it) {
        for (auto it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update();
        }
    }
}

void VariablePool::cleanup(int id) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr || registry->variablePoolShuttingDown) {
        return;
    }
    auto it = registry->variableHolderPool.find(id);
    if (it != registry->variableHolderPool.end()) {
        it->second.clear();
        registry->variableHolderPool.erase(it);
    }
}

void VariablePool::setShuttingDown(bool value) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        return;
    }
    registry->variablePoolShuttingDown = value;
}

void VariablePool::cleanup(const VarsHolder* holder) {
    if (holder == nullptr) {
        return;
    }
    cleanup(holder->getId());
}

template const shared_ptr<Variable<float>> VariablePool::createOrUpdateShared(const string& name, float value);
template const shared_ptr<Variable<glm::vec3>> VariablePool::createOrUpdateShared(const string& name, glm::vec3 value);
template const shared_ptr<Variable<glm::mat4>> VariablePool::createOrUpdateShared(const string& name, glm::mat4 value);
template const shared_ptr<Variable<ofFloatColor>> VariablePool::createOrUpdateShared(const string& name, ofFloatColor value);

template const shared_ptr<Variable<float>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template const shared_ptr<Variable<glm::vec3>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template const shared_ptr<Variable<glm::mat4>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template const shared_ptr<Variable<ofFloatColor>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
