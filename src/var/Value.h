#pragma once

class TexData;

template <typename T>
class Value {
public:
    Value();
    T& get();
    T get() const;
    void set(const T& value);
    
    int index;
//    string subtype = "";
//    int chan = -1;
    T value;
//    float speed = 1.f;
};
