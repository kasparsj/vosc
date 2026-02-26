#include "GeomPool.h"
#include "ResourceRegistry.hpp"

bool GeomPool::hasShared(string name) {
    ResourceRegistry* registry = ResourceRegistry::current();
    return registry != nullptr && registry->geomSharedPool.find(name) != registry->geomSharedPool.end();
}

shared_ptr<Geom> GeomPool::getShared(string name, bool create) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "GeomPool::getShared called without active ResourceRegistry";
        return nullptr;
    }
    if (create && !hasShared(name)) {
        registry->geomSharedPool[name] = make_shared<Geom>();
    }
    auto it = registry->geomSharedPool.find(name);
    if (it == registry->geomSharedPool.end()) {
        return nullptr;
    }
    return it->second;
}

shared_ptr<Geom> GeomPool::getOrCreate(string name, int layerId) {
    if (hasShared(name)) {
        return getShared(name);
    }
    else {
        return getOrCreate(layerId);
    }
}

shared_ptr<Geom> GeomPool::getOrCreate(int layerId) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        ofLogError() << "GeomPool::getOrCreate called without active ResourceRegistry";
        return nullptr;
    }
    if (registry->geomLayerPool.find(layerId) == registry->geomLayerPool.end()) {
        registry->geomLayerPool[layerId] = make_shared<Geom>();
    }
    return registry->geomLayerPool.at(layerId);
}

void GeomPool::update() {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        return;
    }
    for (map<string, shared_ptr<Geom>>::iterator it=registry->geomSharedPool.begin(); it!=registry->geomSharedPool.end(); ++it) {
        it->second->update();
    }
    for (map<int, shared_ptr<Geom>>::iterator it=registry->geomLayerPool.begin(); it!=registry->geomLayerPool.end(); ++it) {
        it->second->update();
    }
}

void GeomPool::clean(int layerId) {
    ResourceRegistry* registry = ResourceRegistry::current();
    if (registry == nullptr) {
        return;
    }
    registry->geomLayerPool.erase(layerId);
}
