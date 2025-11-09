#pragma once

#include "ofMain.h"

class Button {
public:
    Button(float x, float y, float width, float height);
    
    void setPosition(float x, float y);
    void setSize(float width, float height);
    void setBounds(float x, float y, float width, float height);
    
    bool hitTest(float x, float y) const;
    
    void draw() const;
    void draw(const string& label) const;
    
    float getX() const { return x; }
    float getY() const { return y; }
    float getWidth() const { return width; }
    float getHeight() const { return height; }
    
    void setFill(bool fill) { this->fill = fill; }
    void setColor(const ofColor& color) { this->color = color; }
    void setLabelColor(const ofColor& color) { this->labelColor = color; }
    
private:
    float x, y;
    float width, height;
    bool fill;
    ofColor color;
    ofColor labelColor;
};

