#include "TexturePool.h"

map<int, map<string, Texture>*> createTexturePool() {
    map<string, Texture>* shared = new map<string, Texture>;
    map<int, map<string, Texture>*> pool;
    pool[-1] = shared;
    return pool;
}

map<int, map<string, Texture>*> TexturePool::pool = createTexturePool();

bool TexturePool::hasShared(string name) {
    const map<string, Texture>* sharedPool = pool[-1];
    return sharedPool->find(name) != sharedPool->end();
}

Texture& TexturePool::getShared(string name, bool create) {
    map<string, Texture>* sharedPool = pool[-1];
    if (create && !hasShared(name)) {
        (*sharedPool)[name] = Texture();
    }
    return (*sharedPool)[name];
}

Texture& TexturePool::getForShader(string source, int shaderId) {
    if (source != "" && hasShared(source)) {
        return getShared(source);
    }
    else {
        map<string, Texture>* shaderPool = getShaderPool(shaderId);
        (*shaderPool)[source] = Texture();
        return (*shaderPool)[source];
    }
}

map<string, Texture>* TexturePool::getShaderPool(int shaderId) {
    map<string, Texture>* shaderPool = pool[shaderId];
    if (shaderPool == NULL) {
        shaderPool = new map<string, Texture>();
        pool[shaderId] = shaderPool;
    }
    return pool[shaderId];
}

void TexturePool::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    for (map<int, map<string, Texture>*>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        for (map<string, Texture>::iterator it2=it->second->begin(); it2!=it->second->end(); ++it2) {
            it2->second.update(sounds, notes);
        }
    }
}

void TexturePool::clean(int shaderId) {
    delete pool[shaderId];
    pool.erase(shaderId);
}
