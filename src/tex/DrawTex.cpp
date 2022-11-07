#include "DrawTex.h"
#include "Layer.h"

vector<string> DrawTex::draws = {"rect", "gridplane"};

string DrawTex::random() {
    draws[ofRandom(draws.size())];
}

void DrawTex::update(Layer* layer, Texture* tex) {
    ofEnableBlendMode(tex->blendMode);
    fbo.begin();
    if (!tex->noClear) {
        ofClear(0, 0, 0, 0);
    }
    if (path == "rect") {
        glm::vec2 size = tex->getSize();
        ofDrawRectangle(0, 0, size.x, size.y);
    }
    else if (path == "gridplane") {
        ofDrawGridPlane(args[0], args[1], args[2]);
    }
    fbo.end();
    ofDisableBlendMode();
}

void DrawTex::choose() {
    path = random();
}
