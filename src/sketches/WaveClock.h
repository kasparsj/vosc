#pragma once

#include "Sketch.h"

class WaveClock : public Sketch {
    float _angNoise, _radiusNoise;
    float _xNoise, _yNoise;
    float _angle = -PI/2;
    float _radius;
    float _strokeAlpha = 254;
    int _strokeChange = -1;
    int _mode = 0;

    void init() override {
        _angNoise = ofRandom(10);
        _radiusNoise = ofRandom(10);
        _xNoise = ofRandom(10);
        _yNoise = ofRandom(10);
        _mode = 0;
        initialized = true;
    }
  
    void draw(TexData& data) override{
        float _w = data.size.x;
        float _h = data.size.y;
        float _r = min(_w, _h) - 150;
        _radiusNoise += 0.005;
        _radius = (ofNoise(_radiusNoise, data.randomSeed) * _r) +1;
    
        _angNoise += 0.005;
        _angle += (ofNoise(_angNoise, data.randomSeed) * 6) -3;
        if (_angle > 360)  { _angle -= 360;}
        if (_angle < 0)    { _angle += 360;}
        
        _xNoise += 0.01;
        _yNoise += 0.01;
        float centerX = _w/2 + (ofNoise(_xNoise, data.randomSeed) * 100) - 50;
        float centerY = _h/2 + (ofNoise(_yNoise, data.randomSeed) * 100) - 50;
        
        float rad = ofDegToRad(_angle);
        float x1 = centerX + (_radius * cos(rad));
        float y1 = centerY + (_radius * sin(rad));
        
        float opprad = rad + PI;
        float x2 = centerX + (_radius * cos(opprad));
        float y2 = centerY + (_radius * sin(opprad));
        
        if (data.hasVar("strokeAlpha")) {
            _strokeAlpha = data.getVar("strokeAlpha") * 255;
        }
        else {
            _strokeAlpha += _strokeChange;
            if (_strokeAlpha > 254)  {_strokeChange = -1;}
            if (_strokeAlpha < 0)    {_strokeChange =1;}
        }
        
        ofPushStyle();
        ofSetColor(data.getColor(), (int) (_strokeAlpha * 0.75));
        ofSetLineWidth(1);

        ofPushMatrix();
        switch (_mode) {
          case 0:
            ofDrawLine(x1,y1,x2,y2);
            break;
          case 1:
            ofDrawTriangle(x1,y1,x2+abs(y1-y2)/2,y2,x2-abs(y1-y2)/2,y2);
            break;
          case 2:
            ofSetRectMode(OF_RECTMODE_CORNER);
            ofDrawRectangle(min(x1, x2), min(y1, y2), abs(x1-x2), abs(y1-y2));
            break;
          case 3:
            ofDrawEllipse(min(x1, x2), min(y1, y2), abs(x1-x2), abs(y1-y2));
            break;
        }
        ofPopMatrix();
        ofPopStyle();
  }
};
