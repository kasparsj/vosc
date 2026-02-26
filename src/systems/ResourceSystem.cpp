#include "ResourceSystem.hpp"
#include "../GeomPool.h"
#include "../TexturePool.h"
#include "../var/VariablePool.h"

void ResourceSystem::setup() {
    resources.activate();
    VariablePool::setShuttingDown(false);
}

void ResourceSystem::update(const vector<TidalNote>& notes) {
    VariablePool::update(notes);
    TexturePool::update(notes);
    GeomPool::update();
}

void ResourceSystem::shutdown() {
    VariablePool::setShuttingDown(true);
    resources.clear();
    resources.deactivate();
}
