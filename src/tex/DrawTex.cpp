#include "DrawTex.h"
#include "Layer.h"

vector<string> DrawTex::draws = {"rect", "gridplane"};

string DrawTex::random() {
    return draws[ofRandom(draws.size())];
}

void DrawTex::update(TexData& data) {
    FboTex::update(data);
    ofEnableBlendMode(data.blendMode);
    fbo.begin();
    if (!data.noClear) {
        ofClear(0, 0, 0, 0);
    }
    if (path == "rect") {
        glm::vec2 size = data.getSize();
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
