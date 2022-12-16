#pragma once

#include "ofMain.h"
#include "Sound.h"
#include "ofxTidalCycles.h"
#include "ofxOsc.h"
#include "Args.h"
#include "ofxExpr.hpp"

class TexData;

template <typename T>
class Value {
public:
    Value();
    T& get();
    T get() const;
    void set(const string& expr);
    void set(const vector<string>& expr, int i = 0);
    void addSharedVars();
    void set(const T& value);
    void set(const ofxOscMessage& m, int i);
    void update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, int index, int total, TexData* data = NULL);
    void afterDraw();
    
    int index;
    string type = "";
    string subtype = "";
    int chan = -1;
    T value;
    float speed = 1.f;
    ofxExpr<T> expr;
};
