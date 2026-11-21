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
    void set(const vector<string>& expr, const vector<bool>& expressionMask);
    bool validateComponentExpressions();
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
    vector<string> getExpressions() const {
        vector<string> result;
        if (_isExpr) {
            if (componentExpressionMode) {
                result = configuredExpressions;
            } else {
                for (size_t i = 0; i < expr.size(); ++i) result.push_back(expr[i]->getExpression());
            }
        }
        return result;
    }
    bool isExpressionComponent(size_t index) const {
        return index >= expressionMask.size() || expressionMask[index];
    }

private:
    vector<T> values;
    bool _isExpr = true;
    vector<bool> expressionMask;
    vector<string> configuredExpressions;
    bool componentExpressionMode = false;
    // Use float for int expressions since ofxExpr<int> doesn't exist
    using ExprType = typename std::conditional<std::is_same<T, int>::value, ofxExpr<float>, ofxExpr<T>>::type;
    ExprType expr;
    vector<shared_ptr<ofxExpr_<float>>> componentExpressions;
    // todo: needed only for Variable<ofxExprNode>
    vector<glm::mat4> matrices;
};
