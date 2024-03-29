#pragma once

#include "Sketch.h"

class Spiral : public Sketch {
    void init() override {
        initialized = true;
    }

    void draw(TexData& data) override {
        float lastx = -999;
        float lasty = -999;
        
        int strokeAlpha = 255;
        if (data.hasVar("strokeAlpha")) {
            strokeAlpha = data.getVar("strokeAlpha") * 255;
        }
        
        ofClear(0, 0, 0, 0);

        ofPushStyle();
        ofSetLineWidth(20);
        ofSetColor(data.getVarColor("color"), (int) (strokeAlpha * 0.6));
        glm::vec2 size = data.getSize();
        for(float ang = 0; radius <= size.x/2.5; ang +=step){
            radius +=radiusIncrement;
            float rad = ofDegToRad(ang);
            x = size.x/2.f + (radius * cos(rad));
            y = size.y/2.f + (radius * sin(rad));

            if (lastx > -999){
                ofDrawLine(x,y,lastx,lasty);
            }
            lastx=x;
            lasty=y;
        }
        ofPopStyle();

        step+=stepIncrement;
        radius = 0;
    }
    
    float ang = 0;
    float x,y;
    float radius = 0;
    float radiusIncrement = 3.0;
    float step = 0.0;
    float stepIncrement = 0.1;
};
