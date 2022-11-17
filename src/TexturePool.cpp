#include "TexturePool.h"

map<int, map<string, Texture*>> TexturePool::pool;

bool TexturePool::hasShared(string name) {
    const map<string, Texture*>& sharedPool = pool[-1];
    return sharedPool.find(name) != sharedPool.end();
}

Texture* TexturePool::getShared(string name, bool create) {
    map<string, Texture*>& sharedPool = pool[-1];
    if (create && !hasShared(name)) {
        sharedPool[name] = new Texture();
    }
    return sharedPool[name];
}

Texture* TexturePool::getForShader(string name, int shaderId) {
    map<string, Texture*>& shaderPool = getShaderPool(shaderId);
    shaderPool[name] = new Texture();
    return shaderPool[name];
}

map<string, Texture*>& TexturePool::getShaderPool(int shaderId) {
    return pool[shaderId];
}

void TexturePool::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    for (map<int, map<string, Texture*>>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        for (map<string, Texture*>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(sounds, notes);
        }
    }
}

void TexturePool::clean(int shaderId) {
    for (map<string, Texture*>::iterator it2=pool[shaderId].begin(); it2!=pool[shaderId].end(); ++it2) {
        delete it2->second;
    }
    pool.erase(shaderId);
}
