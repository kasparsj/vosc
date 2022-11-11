#pragma once

#include "Sketch.h"

class NoisySpirals : public Sketch {
public:
    void draw(TexData& data) override {
        float width = data.size.x;
        float height = data.size.y;
        if (radius >= endAngle) {
            lastx = -999;
            lasty = -999;
            radius = 10;
            radiusNoise = ofRandom(10);
            strokeWeight = 10.0 / (10+ofRandom(10));
            
            startAngle = int(ofRandom(360));
            endAngle = 0 + int(ofRandom(width/2));
            angleStep = 5 + int(ofRandom(3));
            ang = startAngle;
        }
        else {
            float x,y;
            
            radiusNoise +=0.05;
            radius += 0.5;
            float thisRadius = radius + (ofNoise(radiusNoise) * 200) -100;
            float rad = ofDegToRad(ang);
            x = width/2 + (thisRadius * cos(rad));
            y = height/2 + (thisRadius * sin(rad));
            
            if(lastx > -999){
                int strokeAlpha;
                if (data.hasVar("strokeAlpha")) {
                    strokeAlpha = data.getVar("strokeAlpha") * 255;
                }
                else {
                    strokeAlpha = 127+ofRandom(128);
                }
                ofPushStyle();
                ofNoFill();
                ofSetLineWidth(strokeWeight);
                ofSetColor(data.getColor(), strokeAlpha);
                ofPushMatrix();
                ofDrawLine(x, y, lastx,lasty);
                ofPopMatrix();
                ofPopStyle();
            }
            lastx=x;
            lasty=y;
            ang += (angleStep * data.speed);
        }
    }
    
private:
    float ang;
    int startAngle;
    int endAngle = 0;
    int angleStep;
    float lastx = -999;
    float lasty = -999;
    float radius = 0;;
    float radiusNoise;
    float strokeWeight = 1.0;
};
