#pragma once

#include "Texture.h"

class TexturePool {
public:
    static bool hasShared(string name);
    static shared_ptr<Texture>& getShared(string name, bool create = false);
    static shared_ptr<Texture>& getOrCreate(string name, const VarsHolder* holder);
    static map<string, shared_ptr<Texture>>& getPool(const VarsHolder* holder);
    static void update(const vector<OSCInput>& inputs, const vector<TidalNote>& notes);
    static void clean(int shaderId);
    
private:
    static map<string, shared_ptr<Texture>> sharedPool;
    static map<int, map<string, shared_ptr<Texture>>> shaderPool;
};
