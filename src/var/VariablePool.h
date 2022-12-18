#pragma once

#include "ofxOsc.h"
#include "Variable.h"

class VarsHolder;

class VariablePool {
public:
    static shared_ptr<BaseVar>& get(string name, const VarsHolder* holder);
    static shared_ptr<BaseVar>& createOrUpdate(string name, const ofxOscMessage& m, int idx, const VarsHolder* holder);
    template<typename T>
    static Variable<T>* getOrCreate(string name, const VarsHolder* holder);
    static map<string, shared_ptr<BaseVar>>& getPool(const VarsHolder* holder);
    static bool hasShared(string name);
    static shared_ptr<BaseVar>& getShared(string name);
    static shared_ptr<BaseVar>& createOrUpdateShared(const string& name, const ofxOscMessage& m, int idx);
    static shared_ptr<BaseVar>& createOrUpdateShared(const string& command, const string& name, const ofxOscMessage& m, int idx = 1);
    template<typename T>
    static Variable<T>* createOrUpdateShared(const string& name, T value);
    static shared_ptr<BaseVar> create(const ofxOscMessage& m, int idx = 1);
    static shared_ptr<BaseVar> create(const string& command, const ofxOscMessage& m, int idx);
    static shared_ptr<BaseVar> create(const ofxOscMessage& m, int idx, size_t size);
    static void update(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx);
    static void update(shared_ptr<BaseVar>& var, const string& command, const ofxOscMessage& m, int idx);
    static void update(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx, size_t size);
    static void updateColorsScheme(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx);
    static void update(const vector<OSCInput>& inputs, const vector<TidalNote>& notes);
    static void cleanup(const VarsHolder* holder);
    
private:
    static map<string, shared_ptr<BaseVar>> sharedPool;
    static map<int, map<string, shared_ptr<BaseVar>>> holderPool;
};
