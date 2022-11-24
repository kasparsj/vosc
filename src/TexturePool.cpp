#include "TexturePool.h"

map<string, Texture*> TexturePool::sharedPool;
map<int, map<string, Texture*>> TexturePool::shaderPool;

bool TexturePool::hasShared(string name) {
    return sharedPool.find(name) != sharedPool.end();
}

Texture* TexturePool::getShared(string name, bool create) {
    if (create && !hasShared(name)) {
        sharedPool[name] = new Texture();
    }
    return sharedPool[name];
}

Texture* TexturePool::getForShader(string name, int shaderId) {
    map<string, Texture*>& pool = getShaderPool(shaderId);
    pool[name] = new Texture();
    return pool[name];
}

map<string, Texture*>& TexturePool::getShaderPool(int shaderId) {
    return shaderPool[shaderId];
}

void TexturePool::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    for (map<string, Texture*>::iterator it=sharedPool.begin(); it!=sharedPool.end(); ++it) {
        it->second->update(sounds, notes);
    }
    for (map<int, map<string, Texture*>>::iterator it=shaderPool.begin(); it!=shaderPool.end(); ++it) {
        for (map<string, Texture*>::iterator it2=it->second.begin(); it2!=it->second.end(); ++it2) {
            it2->second->update(sounds, notes);
        }
    }
}

void TexturePool::clean(int shaderId) {
    for (map<string, Texture*>::iterator it2=shaderPool[shaderId].begin(); it2!=shaderPool[shaderId].end(); ++it2) {
        delete it2->second;
    }
    shaderPool.erase(shaderId);
}
