#pragma once

#include "Geom.h"

class GeomPool {
public:
    static map<int, map<string, Geom>*> pool;
    
    static bool hasShared(string name);
    static Geom& getShared(string name, bool create = false);
    
    static Geom& getForLayer(string name, int layerId);
    static void update();
    static void clean(int layerId);
};
