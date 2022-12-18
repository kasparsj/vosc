#pragma once

#include "Geom.h"

class GeomPool {
public:
    static bool hasShared(string name);
    static shared_ptr<Geom>& getShared(string name, bool create = false);
    static shared_ptr<Geom>& getOrCreate(string name, int layerId);
    static shared_ptr<Geom>& getOrCreate(int layerId);
    static void update();
    static void clean(int layerId);
    
private:
    static map<string, shared_ptr<Geom>> sharedPool;
    static map<int, shared_ptr<Geom>> layerPool;
};
