#include "ResourceRegistry.hpp"

ResourceRegistry* ResourceRegistry::currentRegistry = nullptr;

ResourceRegistry::~ResourceRegistry() {
    deactivate();
}

void ResourceRegistry::activate() {
    currentRegistry = this;
}

void ResourceRegistry::deactivate() {
    if (currentRegistry == this) {
        currentRegistry = nullptr;
    }
}

ResourceRegistry* ResourceRegistry::current() {
    return currentRegistry;
}

bool ResourceRegistry::hasCurrent() {
    return currentRegistry != nullptr;
}

void ResourceRegistry::clear() {
    variablePoolShuttingDown = true;

    // Destruction order matters: shader-owned state first, then texture/geom pools,
    // then variable pools.
    shaderSharedPool.clear();
    textureShaderPool.clear();
    textureSharedPool.clear();
    geomLayerPool.clear();
    geomSharedPool.clear();
    variableHolderPool.clear();
    variableSharedPool.clear();
}
