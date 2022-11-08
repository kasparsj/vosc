#pragma once

#include "Geom.h"

class GeomPool {
public:
    static map<string, Geom> sharedPool;
    static map<int, Geom> layerPool;
    
    static bool hasShared(string name);
    static Geom& getShared(string name, bool create = false);
    static Geom& getForLayer(string name, int layerId);
    static Geom& getForLayer(int layerId);
    static void update();
    static void clean(int layerId);
};
