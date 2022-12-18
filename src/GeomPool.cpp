#include "GeomPool.h"

map<string, shared_ptr<Geom>> GeomPool::sharedPool = {};
map<int, shared_ptr<Geom>> GeomPool::layerPool = {};

bool GeomPool::hasShared(string name) {
    return sharedPool.find(name) != sharedPool.end();
}

shared_ptr<Geom>& GeomPool::getShared(string name, bool create) {
    if (create && !hasShared(name)) {
        sharedPool[name] = make_shared<Geom>();
    }
    return sharedPool.at(name);
}

shared_ptr<Geom>& GeomPool::getOrCreate(string name, int layerId) {
    if (hasShared(name)) {
        return getShared(name);
    }
    else {
        return getOrCreate(layerId);
    }
}

shared_ptr<Geom>& GeomPool::getOrCreate(int layerId) {
    if (layerPool.find(layerId) == layerPool.end()) {
        layerPool[layerId] = make_shared<Geom>();
    }
    return layerPool.at(layerId);
}

void GeomPool::update() {
    for (map<string, shared_ptr<Geom>>::iterator it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second->update();
    }
    for (map<int, shared_ptr<Geom>>::iterator it=layerPool.begin(); it!=layerPool.end(); ++it) {
        it->second->update();
    }
}

void GeomPool::clean(int layerId) {
    layerPool.erase(layerId);
}
