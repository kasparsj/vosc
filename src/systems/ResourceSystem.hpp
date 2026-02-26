#pragma once

#include "../ResourceRegistry.hpp"
#include "ofxTidalCycles.h"

class ResourceSystem {
public:
    void setup();
    void update(const vector<TidalNote>& notes);
    void shutdown();

    ResourceRegistry& registry() {
        return resources;
    }

private:
    ResourceRegistry resources;
};
