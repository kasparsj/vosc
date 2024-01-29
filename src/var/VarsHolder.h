#pragma once

#include "Variable.h"

class VarsHolder {
public:
    VarsHolder() {
        static int nextId = 0;
        _id = nextId++;
    }
    virtual ~VarsHolder() {}
    const shared_ptr<BaseVar>& getVariable(const string& name) const;
    bool hasVar(const string& name) const;
    float getVar(const string& name, int idx = 0) const;
    float getVarPercent(const string& name, int idx = 0) const;
    bool getVarBool(const string& name, int idx = 0) const;
    glm::vec3 getVarVec3(const string& name, glm::vec3 defVal) const;
    glm::vec3 getVarVec3(const string& name) const {
        return getVarVec3(name, glm::vec3());
    }
    const vector<float>& getVarVec(const string& name) const;
    vector<float>& getVarVec(const string& name);
    ofFloatColor getVarColor(const string& name) const;
    
    template <typename T>
    const shared_ptr<Variable<T>>& setVar(const string& name, T value);
    template <typename T>
    const shared_ptr<Variable<T>>& setVar(const string& name, vector<T> value);
    const shared_ptr<Variable<float>>& setVar(const string& name, bool value);
    void setVar(const string& name, const ofxOscMessage& value, int idx = 1);
    int getId() const {
        return _id;
    }

    map<string, shared_ptr<BaseVar>> vars;
protected:
    unsigned int _id;
};
