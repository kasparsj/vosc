#pragma once

#include "Texture.h"

class TexturePool {
public:
    static map<string, Texture> pool;
    
    static Texture& get(string name, bool create = false);
    static Texture& getOrCreate(string name, int shaderId);
    static void update();
    static void clean(int shaderId);
};
