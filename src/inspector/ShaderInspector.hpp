#pragma once

#include "ofMain.h"
#include "../shader/Shader.h"
#include "../ui/Button.h"

class ShaderInspector {
public:
    ShaderInspector();
    void draw();
    void mousePressed(int x, int y, int button);
    void mouseReleased(int x, int y, int button);
    void keyPressed(int key);
    
    void setShader(Shader* shader, const string& name = "");
    void clear();
    bool isEmpty() const { return shader == nullptr; }
    
private:
    void drawShaderInfo();
    void drawShaderUniforms();
    void drawShaderTextures();
    void drawShaderBuffers();
    
    Shader* shader = nullptr;
    string shaderName;
    
    // Buttons
    Button printUniformsButton;
    Button backButton;
};

