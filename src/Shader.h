#pragma once

#include "Source.h"
#include "ofMain.h"
#include "Config.h"
#include "ShaderData.h"

class Shader : public Source {
public:
    static map<string, ofShader> shaders;
    
	Shader();
    ~Shader();
	void update(ShaderData *shaderData, Config &config);
	void draw(int left, int top, int width, int height) override;
    void choose();
    void reload();

	ofFbo fbo;
    string prevName = "";
    bool noClear;
    int random = 0;
};

