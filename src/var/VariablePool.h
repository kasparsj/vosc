#pragma once

#include "ofxOsc.h"
#include "Variable.h"

class VarsHolder;

class VariablePool {
public:
    static shared_ptr<BaseVar>& get(const string& name, const VarsHolder* holder);
    static shared_ptr<BaseVar>& createOrUpdate(const string& name, const ofxOscMessage& m, int idx, const VarsHolder* holder);
    template<typename T>
    static Variable<T>* getOrCreate(const string& name, const VarsHolder* holder);
    static map<string, shared_ptr<BaseVar>>& getPool(const VarsHolder* holder);
    static bool hasShared(const string& name);
    static shared_ptr<BaseVar>& getShared(const string& name);
    static shared_ptr<BaseVar>& createOrUpdateShared(const string& name, const ofxOscMessage& m, int idx);
    static shared_ptr<BaseVar>& createOrUpdateShared(const string& command, const string& name, const ofxOscMessage& m, int idx = 1);
    template<typename T>
    static Variable<T>* createOrUpdateShared(const string& name, T value);
    static void update(const vector<OSCInput>& inputs, const vector<TidalNote>& notes);
    static void cleanup(const VarsHolder* holder);
    
private:
    static map<string, shared_ptr<BaseVar>> sharedPool;
    static map<int, map<string, shared_ptr<BaseVar>>> holderPool;
};
