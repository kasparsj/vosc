#include "Webcam.h"
#include "Layer.h"

bool Webcam::exists(int device) {
    ofVideoGrabber grabber;
    std::vector<ofVideoDevice> devices = grabber.listDevices();
    for (int i=0; i<devices.size(); i++) {
        if (devices[i].id == device) {
            return true;
        }
    }
    return false;
}

int Webcam::random() {
    ofVideoGrabber grabber;
    std::vector<ofVideoDevice> devices = grabber.listDevices();
    return devices[int(ofRandom(devices.size()))].id;
}

void Webcam::update(Layer *layer) {
    if (!vidGrabber.isInitialized()) {
        vidGrabber.setDeviceID(stoi(path));
        vidGrabber.setup(layer->size.x, layer->size.y);
    }
    vidGrabber.update();
}

void Webcam::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    ofPushStyle();
    ofSetColor(255);
    vidGrabber.draw(pos.x-size.x/2.f, pos.y-size.y/2.f, size.x, size.y);
    ofPopStyle();
}

void Webcam::choose() {
    path = ofToString(random());
}

ofFloatColor Webcam::getTint(Layer *layer) {
    return layer->getColor();
}
