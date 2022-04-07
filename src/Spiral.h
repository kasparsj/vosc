#pragma once

#include "SketchImpl.h"

class Spiral : public SketchImpl {
    void init() override {
        initialized = true;
    }

    void draw(Layer *layer) override {
        float lastx = -999;
        float lasty = -999;
        
        int strokeAlpha = 255;
        if (layer->data->values.size()) {
            strokeAlpha = layer->data->values[0] * 255;
        }
        
        ofClear(0, 0, 0, 0);

        ofPushStyle();
        ofSetLineWidth(20);
        ofSetColor(layer->getColor(), (int) (strokeAlpha * 0.6));
        for(float ang = 0; radius <=layer->size.x/2.5; ang +=step){
            radius +=radiusIncrement;
            float rad = ofDegToRad(ang);
            x = layer->size.x/2.f + (radius * cos(rad));
            y = layer->size.y/2.f + (radius * sin(rad));

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
