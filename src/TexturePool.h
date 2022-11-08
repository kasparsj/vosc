#pragma once

#include "Texture.h"

class TexturePool {
public:
    static map<int, map<string, Texture>*> pool;
    
    static bool hasShared(string name);
    static Texture& getShared(string name, bool create = false);
    static Texture& getForShader(string name, int shaderId);
    static void update();
    static void clean(int shaderId);
};
