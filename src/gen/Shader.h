#pragma once

#include "FBOGen.h"
#include "ofMain.h"

class Shader : public FBOGen {
public:
    static map<string, ofShader> cache;
    static string random();
    
    Shader(string name) : FBOGen(name) {}
    ~Shader() {}
	void update(Layer *layer) override;
	void draw(int left, int top, int width, int height) override;
    void choose() override;
    void reload() override;
};

