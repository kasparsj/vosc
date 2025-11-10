#pragma once

#include "ofMain.h"
#include "../var/VarsHolder.h"

class VarsInspector {
public:
    VarsInspector();
    void draw(const VarsHolder* holder, int x, int y);
    void draw(const map<string, shared_ptr<BaseVar>>& vars, int x, int y);
    
private:
    string formatVariableValue(const shared_ptr<BaseVar>& var);
};

