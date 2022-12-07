#pragma once

#include "ofMain.h"
#include "Variable.h"

class VarsHolder {
public:
    VarsHolder() {
        static int nextId = 0;
        _id = nextId++;
    }
    virtual ~VarsHolder() {}
    const Variable* getVariable(string name) const;
    bool hasVar(string name) const;
    float getVar(string name, int idx = 0) const;
    bool getVarBool(string name, int idx = 0) const;
    glm::vec3 getVarVec3(string name, glm::vec3 defVal) const;
    glm::vec3 getVarVec3(string name) const {
        return getVarVec3(name, glm::vec3());
    }
    vector<float> getVarVec(string name) const;
    ofFloatColor getVarColor(string name) const;
    void setVar(string name, float value);
    void setVar(string name, vector<float> value);
    void setVar(string name, glm::vec3 value);
    void setVar(string name, ofFloatColor value);
    void setVar(string name, const ofxOscMessage& value, int idx = 1);
    int getId() {
        return _id;
    }

    map<string, Variable*> vars;
protected:
    void setVar(string name, Variable* var);
    
    unsigned int _id;
};
