#pragma once

#include "ofConstants.h"
#include "ofxTidalCycles.h"
#include "Sound.h"
#include "Config.h"
#include "LayerData.h"
#include "Gen.h"
#include "ofxLooper.h"
#include "Geom.h"

#define MAX_DELAY 120

class Layer {
public:
    static Gen* factory(string type, string name);
    static Gen* factory(string source);
    
    ~Layer() {
        unload();
    }
    void setup(int index, string dataSource = "");
    void layout(Layout layout, int layoutIndex, int layoutTotal);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void drawToFbo();
    void draw(const glm::vec3 &pos, const glm::vec3 &size);
    void draw(int totalVisible);
    void setGeometry(string key);
    void setDataSources(vector<string> ds);
    void addDataSources(vector<string> ds);
    void load(string source);
    void choose(string type = "");
    void unload();
    void reload();
    void clear();
    void reset();
    void resetTransform();
    
    ofFloatColor getColor() {
        if (useMFCCColor && data->mfcc.size() > 0) {
            return data->getMFCCColor();
        }
        return color;
    }
    
    bool randomShader() const {
        return behaviour > 0 && behaviour & (int) Behaviour::RANDOM_SHADER;
    }
    
    int index;
    Geom geom;
    vector<ofFbo> frames;
    LayerData* data = NULL;
    Gen* gen = NULL;
    ofxLooper* looper = NULL;
    glm::vec3 pos;
    glm::vec3 size;
    glm::vec3 rotation = glm::vec3(0, 0, 0);
    glm::vec3 rotationPoint = glm::vec3(0, 0, 0);
    glm::vec3 rotationSpeed = glm::vec3(0, 0, 0);
    glm::vec3 scale = glm::vec3(1);
    ofFloatColor color = ofFloatColor(0, 0);
    ofAlignHorz alignH;
    ofAlignVert alignV;
    float speed = 1.f;
    int behaviour = -1;
    float bri = 1.0;
    float alpha = 1.0;
    bool aspectRatio = true;
    float timePct = 0;
    bool noClear;
    bool useMFCCColor = false;
    bool isTidal = false;
    int randomSeed = 0;
    vector<string> dataSources;
    float thresh = 0.5;
    // todo: maybe should by default fallback to thresh
    float onsetThresh = 0.5;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    bool useRandomColor = false;
    uint8_t delay = 0;
    int curFbo = -1;
    bool drawWireframe;
};
