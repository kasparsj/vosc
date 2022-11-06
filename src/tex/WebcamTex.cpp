#include "WebcamTex.h"
#include "Layer.h"

bool WebcamTex::exists(int device) {
    ofVideoGrabber grabber;
    std::vector<ofVideoDevice> devices = grabber.listDevices();
    for (int i=0; i<devices.size(); i++) {
        if (devices[i].id == device) {
            return true;
        }
    }
    return false;
}

int WebcamTex::random() {
    ofVideoGrabber grabber;
    std::vector<ofVideoDevice> devices = grabber.listDevices();
    return devices[int(ofRandom(devices.size()))].id;
}

void WebcamTex::update(Layer *layer) {
    if (!vidGrabber.isInitialized()) {
        vidGrabber.setDeviceID(stoi(path));
        vidGrabber.setup(layer->size.x, layer->size.y);
    }
    vidGrabber.update();
}

void WebcamTex::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    ofPushStyle();
    ofSetColor(255);
    vidGrabber.draw(pos.x, pos.y, size.x, size.y);
    ofPopStyle();
}

void WebcamTex::choose() {
    path = ofToString(random());
}
