#include "GeomPool.h"

map<int, map<string, Geom>*> createGeomPool() {
    map<string, Geom>* shared = new map<string, Geom>;
    map<int, map<string, Geom>*> pool;
    pool[-1] = shared;
    return pool;
}

map<int, map<string, Geom>*> GeomPool::pool = createGeomPool();

bool GeomPool::hasShared(string name) {
    map<string, Geom>* sharedPool = pool[-1];
    return sharedPool->find(name) != sharedPool->end();
}

Geom& GeomPool::getShared(string name, bool create) {
    map<string, Geom>* sharedPool = pool[-1];
    if (create && !hasShared(name)) {
        (*sharedPool)[name] = Geom();
    }
    return (*sharedPool)[name];
}

Geom& GeomPool::getForLayer(string source, int layerId) {
    if (hasShared(source)) {
        return getShared(source);
    }
    else {
        map<string, Geom>* layerPool = pool[layerId];
        if (layerPool == NULL) {
            layerPool = new map<string, Geom>();
        }
        (*layerPool)[source] = Geom();
        return (*layerPool)[source];
    }
}

void GeomPool::update() {
    for (map<int, map<string, Geom>*>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        for (map<string, Geom>::iterator it2=it->second->begin(); it2!=it->second->end(); ++it2) {
            it2->second.update();
        }
    }
}

void GeomPool::clean(int layerId) {
    delete pool[layerId];
    pool.erase(layerId);
}
