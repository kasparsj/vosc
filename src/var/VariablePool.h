#pragma once

#include "Variable.h"

class VarsHolder;

class VariablePool {
public:
    static BaseVar* get(VarsHolder* holder, string name);
    template <typename T>
    static Variable<T>* getOrCreate(VarsHolder* holder, string name);
    static map<string, BaseVar*>& getPool(VarsHolder* holder);
    static bool hasShared(string name);
    static BaseVar* getShared(string name);
    template <typename T>
    static Variable<T>* getOrCreateShared(string name);
    static void update(const vector<Mic>& mics, const vector<Sound>& sounds, const vector<TidalNote>& notes);
    static void cleanup(VarsHolder* holder);
    
private:
    static map<string, BaseVar*> sharedPool;
    static map<int, map<string, BaseVar*>> texturePool;
    static map<int, map<string, BaseVar*>> shaderPool;
    static map<int, map<string, BaseVar*>> layerPool;
};
