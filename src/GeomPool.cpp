#include "GeomPool.h"

map<string, Geom> GeomPool::sharedPool = {};
map<int, Geom> GeomPool::layerPool = {};

bool GeomPool::hasShared(string name) {
    return sharedPool.find(name) != sharedPool.end();
}

Geom& GeomPool::getShared(string name, bool create) {
    if (create && !hasShared(name)) {
        sharedPool[name] = Geom();
    }
    return sharedPool[name];
}

Geom& GeomPool::getForLayer(string name, int layerId) {
    if (hasShared(name)) {
        return getShared(name);
    }
    else {
        return getForLayer(layerId);
    }
}

Geom& GeomPool::getForLayer(int layerId) {
    if (layerPool.find(layerId) == layerPool.end()) {
        layerPool = map<int, Geom>();
    }
    layerPool[layerId] = Geom();
    return layerPool[layerId];
}

void GeomPool::update() {
    for (map<string, Geom>::iterator it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second.update();
    }
    for (map<int, Geom>::iterator it=layerPool.begin(); it!=layerPool.end(); ++it) {
        it->second.update();
    }
}

void GeomPool::clean(int layerId) {
    layerPool.erase(layerId);
}
