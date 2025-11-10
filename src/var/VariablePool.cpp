#include "VariablePool.h"
#include "Texture.h"
#include "shader/Shader.h"
#include "Layer.h"
#include "VarsHolder.h"
#include "ofxExpr.hpp"

map<string, shared_ptr<BaseVar>> VariablePool::sharedPool;
map<int, map<string, shared_ptr<BaseVar>>> VariablePool::holderPool;
bool VariablePool::isShuttingDown = false;

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
        BaseVar::update(sharedPool.at(name), command, m, idx);
    }
    else {
        sharedPool[name] = BaseVar::create(command, m, idx);
    }
    return sharedPool.at(name);
}

template<typename T>
const shared_ptr<Variable<T>> VariablePool::createOrUpdateShared(const string& name, T value) {
    if (!hasShared(name)) {
        sharedPool[name] = make_shared<Variable<T>>();
    }
    auto var = std::static_pointer_cast<Variable<T>>(sharedPool.at(name));
    var->set(value);
    return var;
}

shared_ptr<BaseVar>& VariablePool::get(const string& name, const VarsHolder* holder) {
    map<string, shared_ptr<BaseVar>>& pool = getPool(holder);
    return pool.at(name);
}

shared_ptr<BaseVar>& VariablePool::createOrUpdate(const string& name, const ofxOscMessage& m, int idx, const VarsHolder* holder) {
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
    if (holder == NULL) {
        return sharedPool;
    }
    return holderPool[holder->getId()];
}

void VariablePool::update(const vector<TidalNote> &notes) {
    for (auto it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second->update();
    }
    for (auto it=holderPool.begin(); it!=holderPool.end(); ++it) {
        for (auto it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update();
        }
    }
}

void VariablePool::cleanup(int id) {
    // Directly access holderPool using the ID
    // During shutdown, holderPool might be destroyed, so we need to be careful
    if (isShuttingDown) {
        return;
    }
    // Try to clean up, but if the map is being destroyed, just ignore it
    // During shutdown, static objects are destroyed in reverse order of construction
    // If holderPool is destroyed before objects that call cleanup(), we'll crash
    // So we check the shutdown flag and skip cleanup if we're shutting down
    auto it = holderPool.find(id);
    if (it != holderPool.end()) {
        it->second.clear();
        holderPool.erase(it);
    }
}

void VariablePool::setShuttingDown(bool value) {
    isShuttingDown = value;
}

void VariablePool::cleanup(const VarsHolder* holder) {
    if (holder == NULL) {
        return;
    }
    try {
        // Get the ID before accessing the holder further, in case it's being destroyed
        int id = holder->getId();
        // Call the ID-based version to avoid accessing the holder pointer again
        cleanup(id);
    } catch (...) {
        // During shutdown, the holder might be invalid or holderPool might be destroyed
        // Silently ignore any exceptions to prevent crashes
        isShuttingDown = true;
    }
}

template const shared_ptr<Variable<float>> VariablePool::createOrUpdateShared(const string& name, float value);
template const shared_ptr<Variable<glm::vec3>> VariablePool::createOrUpdateShared(const string& name, glm::vec3 value);
template const shared_ptr<Variable<glm::mat4>> VariablePool::createOrUpdateShared(const string& name, glm::mat4 value);
template const shared_ptr<Variable<ofFloatColor>> VariablePool::createOrUpdateShared(const string& name, ofFloatColor value);

template const shared_ptr<Variable<float>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template const shared_ptr<Variable<glm::vec3>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template const shared_ptr<Variable<glm::mat4>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
template const shared_ptr<Variable<ofFloatColor>> VariablePool::getOrCreate(const string& name, const VarsHolder* holder);
