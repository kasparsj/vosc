#pragma once

#include "BaseVar.h"
#include "ofxOsc.h"
#include "ofxExpr.hpp"
#include <type_traits>

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
    virtual void update() override;
    const vector<T>& getVec() const {
        return values;
    }
    vector<T>& getVec() {
        return values;
    }
    BufData asBufferData() override;
    ofBufferObject asBufferObject();
    virtual void afterDraw() override;
    size_t size() const override {
        return values.size();
    }
    bool isConst() const {
        return !_isExpr;
    }

private:
    vector<T> values;
    bool _isExpr = true;
    // Use float for int expressions since ofxExpr<int> doesn't exist
    using ExprType = typename std::conditional<std::is_same<T, int>::value, ofxExpr<float>, ofxExpr<T>>::type;
    ExprType expr;
    // todo: needed only for Variable<ofxExprNode>
    vector<glm::mat4> matrices;
};
