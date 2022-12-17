#pragma once

#include "BaseVar.h"
#include "ofxOsc.h"
#include "ofxExpr.hpp"

template <typename T>
class Variable : public BaseVar {
public:
    Variable() {}
    Variable(T value) {
        set(value);
    }
    void set(T value);
    void set(vector<T> value);
    void set(const string& expr);
    void set(const vector<string>& expr);
    void addSharedVars();
    void set(const ofxOscMessage& m, int idx = 1);
    T& get(int idx = 0) {
        return values.at(idx);
    }
    T get(int idx = 0) const {
        return values.at(idx);
    }
    virtual void update(const vector<OSCInput> &inputs, const vector<TidalNote> &notes) override;
    vector<T> getVec() const {
        vector<T> vec;
        for (int i=0; i<values.size(); i++) {
            vec.push_back(values.at(i));
        }
        return vec;
    }
    BufData asBufferData() override;
    virtual void afterDraw() override;
    size_t size() const {
        return values.size();
    }

    vector<T> values;
    string type = "";
    ofxExpr<T> expr;
    
    vector<glm::mat4> matrices;
};
