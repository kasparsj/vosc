#pragma once

#include "ofConstants.h"
#include "ofxTidalCycles.h"
#include "Sound.h"
#include "Config.h"
#include "LayerData.h"
#include "LayerGeom.h"
#include "LayerShader.h"

class Layer {
public:
    void setup(int index, string dataSource = "");
    void layout(Layout layout, int layoutIndex, int layoutTotal);
    void update(const vector<Sound> &sounds, const vector<TidalNote> &notes);
    void drawToFbo();
    void draw(const glm::vec3 &pos, const glm::vec3 &size);
    void draw(int totalVisible);
    void doAlign();
    void doRotate();
    void doScale();
    void setDataSources(vector<string> ds);
    void addDataSources(vector<string> ds);
    void reset();
    void resetTransform();    
    void setShader(string path);
    
    int index;
    LayerGeom geom;
    LayerData data;
    LayerShader shader;
    glm::vec3 pos;
    glm::vec3 size;
    glm::vec3 rotation = glm::vec3(0, 0, 0);
    glm::vec3 rotationPoint = glm::vec3(0, 0, 0);
    glm::vec3 rotationSpeed = glm::vec3(0, 0, 0);
    glm::vec3 scale = glm::vec3(1);
    ofAlignHorz alignH;
    ofAlignVert alignV;
    int behaviour = -1;
    float bri = 1.0;
    float alpha = 1.0;
    bool isTidal = false;
    vector<string> dataSources;
    float thresh = 0.5;
    // todo: maybe should by default fallback to thresh
    float onsetThresh = 0.5;
    ofBlendMode blendMode = OF_BLENDMODE_ALPHA;
    uint8_t delay = 0;
    ofMaterialSettings matSettings;
    ofMaterial material;
};
