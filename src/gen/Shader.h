#pragma once

#include "FBOGen.h"
#include "ofMain.h"
#include "Config.h"
#include "VisualData.h"

class Shader : public FBOGen {
public:
    static map<string, ofShader> shaders;
    
	Shader();
    ~Shader();
	void update(VisualData *data, Config &config);
	void draw(int left, int top, int width, int height) override;
    void choose() override;
    void reload();

    bool noClear;
};

