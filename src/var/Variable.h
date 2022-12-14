#pragma once

#include "BaseVar.h"
#include "Value.h"

template <typename T>
class Variable : public BaseVar {
public:
    Variable() {}
    Variable(T value) {
        set(value);
    }
    void set(T value);
    void set(vector<T> value);
    virtual void set(const ofxOscMessage& value, int idx = 1) override;
    T get(int idx = 0) const {
        return values[idx].get();
    }
    virtual void update(const vector<Mic> &mics, const vector<Sound> &sounds, const vector<TidalNote> &notes, TexData* data = NULL) override;
    vector<T> getVec() const {
        vector<T> vec;
        for (int i=0; i<values.size(); i++) {
            vec.push_back(get(i));
        }
        return vec;
    }
    virtual void afterDraw() override;
    size_t size() const {
        return values.size();
    }

    vector<Value<T>> values;
    
private:
    void init(const ofxOscMessage& value, int idx = 1);
    void setValue(const ofxOscMessage& m, int idx = 1);
};
