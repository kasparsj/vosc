#include "TexturePool.h"

map<string, Texture> TexturePool::pool = {};

Texture& TexturePool::get(string which, bool create) {
    if (create && pool.find(which) == pool.end()) {
        pool[which] = Texture();
    }
    return pool[which];
}

Texture& TexturePool::getOrCreate(string source, int shaderId) {
    if (pool.find(source) == pool.end()) {
        pool[ofToString(shaderId) + "__" + source] = Texture();
        return pool[ofToString(shaderId) + "__" + source];
    }
    else {
        return get(source);
    }
}

void TexturePool::update() {
    for (map<string, Texture>::iterator it=pool.begin(); it!=pool.end(); ++it) {
        it->second.update();
    }
}

void TexturePool::clean(int shaderId) {
    
}
