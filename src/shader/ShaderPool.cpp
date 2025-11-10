#include "ShaderPool.h"
#include "ofxTidalCycles.h"

map<string, shared_ptr<Shader>> ShaderPool::sharedPool;

bool ShaderPool::hasShared(string name) {
    return sharedPool.find(name) != sharedPool.end();
}

shared_ptr<Shader>& ShaderPool::getShared(string name, bool create) {
    if (create && !hasShared(name)) {
        sharedPool[name] = make_shared<Shader>();
    }
    return sharedPool.at(name);
}

shared_ptr<Shader>& ShaderPool::getOrCreate(string name) {
    if (sharedPool.find(name) == sharedPool.end()) {
        sharedPool[name] = make_shared<Shader>();
    }
    return sharedPool.at(name);
}

map<string, shared_ptr<Shader>>& ShaderPool::getPool() {
    return sharedPool;
}

void ShaderPool::update(const vector<TidalNote> &notes) {
    for (map<string, shared_ptr<Shader>>::iterator it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second->update(notes);
    }
}

void ShaderPool::clean() {
    sharedPool.clear();
}

