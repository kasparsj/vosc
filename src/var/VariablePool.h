#pragma once

#include "VarsHolder.h"

class VariablePool {
public:
    static Variable* get(VarsHolder* holder, string name);
    static Variable* getOrCreate(VarsHolder* holder, string name);
    static map<string, Variable*>& getPool(VarsHolder* holder);
    static bool hasShared(string name);
    static Variable* getShared(string name, bool create = false);
    static void update(const vector<Mic>& mics, const vector<Sound>& sounds, const vector<TidalNote>& notes);
    static void cleanup(VarsHolder* holder);
    
private:
    static map<string, Variable*> sharedPool;
    static map<int, map<string, Variable*>> texturePool;
    static map<int, map<string, Variable*>> shaderPool;
    static map<int, map<string, Variable*>> layerPool;
};
