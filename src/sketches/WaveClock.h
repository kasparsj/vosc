#pragma once

#include "SketchImpl.h"

class WaveClock : public SketchImpl {
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
  
    void draw(Layer *layer) override{
        float _w = layer->size.x;
        float _h = layer->size.y;
        float _r = min(_w, _h) - 150;
        _radiusNoise += 0.005;
        _radius = (ofNoise(_radiusNoise, layer->randomSeed) * _r) +1;
    
        _angNoise += 0.005;
        _angle += (ofNoise(_angNoise, layer->randomSeed) * 6) -3;
        if (_angle > 360)  { _angle -= 360;}
        if (_angle < 0)    { _angle += 360;}
        
        _xNoise += 0.01;
        _yNoise += 0.01;
        float centerX = _w/2 + (ofNoise(_xNoise, layer->randomSeed) * 100) - 50;
        float centerY = _h/2 + (ofNoise(_yNoise, layer->randomSeed) * 100) - 50;
        
        float rad = ofDegToRad(_angle);
        float x1 = centerX + (_radius * cos(rad));
        float y1 = centerY + (_radius * sin(rad));
        
        float opprad = rad + PI;
        float x2 = centerX + (_radius * cos(opprad));
        float y2 = centerY + (_radius * sin(opprad));
        
        if (layer->data->values.size()) {
            _strokeAlpha = layer->data->values[0] * 255;
        }
        else {
            _strokeAlpha += _strokeChange;
            if (_strokeAlpha > 254)  {_strokeChange = -1;}
            if (_strokeAlpha < 0)    {_strokeChange =1;}
        }
        
        ofPushStyle();
        ofSetColor(layer->getColor(), (int) (_strokeAlpha * 0.75));
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
