#pragma once

#include "Shader.h"

class ShaderPool {
public:
    static bool hasShared(string name);
    static shared_ptr<Shader>& getShared(string name, bool create = false);
    static shared_ptr<Shader>& getOrCreate(string name);
    static map<string, shared_ptr<Shader>>& getPool();
    static void update(const vector<TidalNote>& notes);
    static void clean();
    
private:
    static map<string, shared_ptr<Shader>> sharedPool;
};

