#pragma once

#include "Variable.h"

class VarsHolder {
public:
    VarsHolder() {
        static int nextId = 0;
        _id = nextId++;
    }
    virtual ~VarsHolder() {}
    const shared_ptr<BaseVar>& getVariable(string name) const;
    bool hasVar(string name) const;
    float getVar(string name, int idx = 0) const;
    bool getVarBool(string name, int idx = 0) const;
    glm::vec3 getVarVec3(string name, glm::vec3 defVal) const;
    glm::vec3 getVarVec3(string name) const {
        return getVarVec3(name, glm::vec3());
    }
    vector<float> getVarVec(string name) const;
    ofFloatColor getVarColor(string name) const;
    template <typename T>
    Variable<T>* setVar(string name, T value);
    template <typename T>
    Variable<T>* setVar(string name, vector<T> value);
    Variable<float>* setVar(string name, bool value);
    void setVar(string name, const ofxOscMessage& value, int idx = 1);
    int getId() const {
        return _id;
    }

    map<string, shared_ptr<BaseVar>> vars;
protected:
    unsigned int _id;
};
