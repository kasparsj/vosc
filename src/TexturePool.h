#pragma once

#include "Texture.h"

class TexturePool {
public:
    static map<int, map<string, Texture>*> pool;
    
    static bool hasShared(string name);
    static Texture& getShared(string name, bool create = false);
    static Texture& getForShader(string name, int shaderId);
    static map<string, Texture>* getShaderPool(int shaderId);
    static void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    static void clean(int shaderId);
};
