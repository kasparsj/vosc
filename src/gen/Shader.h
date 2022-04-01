#pragma once

#include "FBOGen.h"
#include "ofMain.h"

class Shader : public FBOGen {
public:
    static map<string, ofShader> shaders;
    
	Shader();
    ~Shader();
	void update(VisualData *data, Config &config) override;
	void draw(int left, int top, int width, int height) override;
    void choose() override;
    void reload();

    bool noClear;
};

