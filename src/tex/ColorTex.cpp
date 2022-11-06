#include "ColorTex.h"
#include "ofMain.h"

bool ColorTex::exists(string path) {
    return (path.substr(0, 1) == "#" && path.size() == 7) || (path.substr(0, 2) == "0x" && path.size() == 8);
}

string ColorTex::random() {
    return "0x" + ofToString(ofColor(ofRandom(0,255),ofRandom(0,255),ofRandom(0,255)).getHex());
}

void ColorTex::update(Layer *layer) {
    if (!pixels.isAllocated()) {
        color = ofColor::fromHex(ofHexToInt(path));
        pixels.allocate(1, 1, OF_PIXELS_RGB);
        pixels.setColor(0, 0, color);
    }
}

void ColorTex::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    ofPushStyle();
    ofSetColor(color);
    ofDrawRectangle(pos.x, pos.y, size.x, size.y);
    ofPopStyle();
}

void ColorTex::choose() {
    path = random();
}
