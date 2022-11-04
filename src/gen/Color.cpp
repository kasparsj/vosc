#include "Color.h"
#include "ofMain.h"

bool Color::exists(string path) {
    return (path.substr(0, 1) == "#" && path.size() == 7) || (path.substr(0, 2) == "0x" && path.size() == 8);
}

string Color::random() {
    return "0x" + ofToString(ofColor(ofRandom(0,255),ofRandom(0,255),ofRandom(0,255)).getHex());
}

void Color::update(Layer *layer) {
    if (!pixels.isAllocated()) {
        color = ofColor::fromHex(ofHexToInt(path));
        pixels.allocate(1, 1, OF_PIXELS_RGB);
        pixels.setColor(0, 0, color);
    }
}

void Color::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    ofPushStyle();
    ofSetColor(color);
    ofDrawRectangle(pos.x, pos.y, size.x, size.y);
    ofPopStyle();
}

void Color::choose() {
    path = random();
}
