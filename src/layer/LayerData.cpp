#include "LayerData.h"
#include "Layer.h"

void LayerData::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    const float timef = ofGetElapsedTimef();
    time += ((timef - prevTime) * layer->speed);
    prevTime = timef;
    const vector<string> &ds = layer->dataSources;
    tidal = ds.size() && ds[0].substr(0, 5) == "tidal";
    if (!tidal) {
        reset();
    }
    values.resize(ds.size());
    if (tidal) {
        for (int i = 0; i < notes.size(); i++) {
            if (ofGetElapsedTimef() - notes[i].timeStamp < 32) {
                //float diff = ofGetElapsedTimef() - notes[i].timeStamp - notes[i].latency;
                float diff = ofGetElapsedTimef() - notes[i].timeStamp;
                if (diff > 0 && abs(diff) < 1.0 / ofGetFrameRate() && notes[i].s != "midi") {
                    int instNum = notes[i].instNum;
                    auto it = find(ds.begin(), ds.end(), "tidal" + ofToString(instNum));
                    if (it != ds.end()) {
                        int idx = it - ds.begin();
                        values[idx] += notes[i].gain;
                        onset = true;
                    }
                }
            }
        }
    }
    else {
        for (int i=0; i<ds.size(); i++) {
            if (ds[i].substr(0, 3) == "amp" || ds[i].substr(0, 4) == "loud") {
                int j;
                if (ds[i].substr(0, 3) == "amp") {
                    j = ofToInt(ds[i].substr(3));
                    values[i] += (sounds[j].amplitude / sounds[j].maxAmp);
                }
                else {
                    j = ofToInt(ds[i].substr(4));
                    values[i] += (sounds[j].loudness / sounds[j].maxLoud);
                }
                onset = onset || (sounds[j].onset == 1);
                mfcc = sounds[j].mfcc;
            }
            else {
                if (ds[i].substr(0, 5) == "const") {
                    values[i] = (ds[i].size() > 5 ? std::stof(ds[i].substr(5)) : 1.0);
                }
                else if (ds[i].substr(0, 5) == "noise") {
                    values[i] = ofNoise(i, time) * (ds[i].size() > 5 ? std::stof(ds[i].substr(5)) : 1.0);
                }
                else if (ds[i].substr(0, 4) == "rand") {
                    values[i] = ofRandom(ds[i].size() > 4 ? std::stof(ds[i].substr(4)) : 1.0);
                }
                else if (ds[i].substr(0, 3) == "sin") {
                    values[i] = abs(sin(time));
                }
                if (values.size() > 0 && i == 0) {
                    onset = values[0] >= layer->onsetThresh;
                    // trigger onset once - only when crossing thresh going up
                    if (onset && prevOnset) {
                        onset = false;
                        prevOnset = true;
                    }
                    else {
                        prevOnset = onset;
                    }
                }
            }
        }
    }
    if (values.size() > 0 && values[0] > 1.f) {
        values[0] = 1.f;
    }
    visible = onset || (values.size() == 0 || values[0] >= layer->thresh);
    
    if (shader.isLoaded()) {
        ofEnableBlendMode( OF_BLENDMODE_DISABLED ); // Important! We just want to write the data as is to the target fbo
        
        fbo.dest()->begin();
        fbo.dest()->activateAllDrawBuffers(); // if we have multiple color buffers in our FBO we need this to activate all of them
        
        shader.begin(layer);
        for (int i=0; i<fbo.source()->getNumTextures(); i++) {
            shader.getShader()->setUniformTexture("tex" + ofToString(i), fbo.source()->getTextureReference(i), i );
        }
                
        fbo.source()->draw(0,0);
        shader.end();
            
        fbo.dest()->end();
        fbo.swap();
    }
}

void LayerData::afterDraw() {
    if (tidal && values.size() >= 1) {
        values[0] -= 1.f/8.f;
        if (values[0] < 0) {
            values[0] = 0;
        }
    }
}

void LayerData::initFbo(const ofxOscMessage& m) {
    // Allocate buffers
    ofFbo::Settings fboSettings;
    fboSettings.width  = m.getArgAsInt(1);
    fboSettings.height = m.getArgAsInt(2);
    
    // We can create several color buffers for one FBO if we want to store velocity for instance,
    // then draw to them simultaneously from a shader using gl_FragData[0], gl_FragData[1], etc.
    fboSettings.numColorbuffers = m.getNumArgs() > 3 ? m.getArgAsInt(3) : 1;
    
    fboSettings.useDepth = false;
    fboSettings.internalformat = GL_RGBA32F;    // Gotta store the data as floats, they won't be clamped to 0..1
    fboSettings.textureTarget = GL_TEXTURE_2D;
    fboSettings.wrapModeHorizontal = GL_CLAMP_TO_EDGE;
    fboSettings.wrapModeVertical = GL_CLAMP_TO_EDGE;
    fboSettings.minFilter = GL_NEAREST; // No interpolation, that would mess up data reads later!
    fboSettings.maxFilter = GL_NEAREST;
    
    ofDisableTextureEdgeHack();
    
        fbo.allocate( fboSettings );
    
    ofEnableTextureEdgeHack();
}