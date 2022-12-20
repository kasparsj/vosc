#pragma once

#include "ofMain.h"
#include "ofxOsc.h"
#include "ofxTidalCycles.h"

class OSCInput;

struct BufData {
    GLsizeiptr bytes;
    const void * data;
};

class BaseVar {
public:
    static shared_ptr<BaseVar> create(const ofxOscMessage& m, int idx = 1);
    static shared_ptr<BaseVar> create(const string& command, const ofxOscMessage& m, int idx);
    static shared_ptr<BaseVar> createVar(const ofxOscMessage& m, int idx, size_t size);
    static shared_ptr<BaseVar> createVar(const ofxOscMessage& m, int idx);
    static shared_ptr<BaseVar> createVar2(const ofxOscMessage& m, int idx);
    static void update(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx);
    static void update(shared_ptr<BaseVar>& var, const string& command, const ofxOscMessage& m, int idx);
    static void updateVar(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx, size_t size);
    static void updateVar(shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx);
    static void updateColorsScheme(const shared_ptr<BaseVar>& var, const ofxOscMessage& m, int idx);

    virtual size_t size() const { return 1; }
    virtual void update() = 0;
    virtual void afterDraw() = 0;
    virtual BufData asBufferData() = 0;
};
