#include "DrawTex.h"
#include "Layer.h"

vector<string> DrawTex::draws = {"rect", "gridplane"};

string DrawTex::random() {
    draws[ofRandom(draws.size())];
}

void DrawTex::update(Layer *layer) {

}

void DrawTex::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    if (path == "rect") {
        ofDrawRectangle(pos.x, pos.y, size.x, size.y);
    }
    else if (path == "gridplane") {
        ofDrawGridPlane(args[0], args[1], args[2]);
    }
}

void DrawTex::choose() {
    path = random();
}
