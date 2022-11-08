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

void WebcamTex::update(TexData& data) {
    if (!vidGrabber.isInitialized()) {
        vidGrabber.setDeviceID(stoi(path));
        vidGrabber.setup(args.size() > 0 ? args[0] : 1920,
                         args.size() > 1 ? args[1] : 1080);
        data.setSize(vidGrabber.getWidth(), vidGrabber.getHeight());
    }
    vidGrabber.update();
    aspectRatio = data.aspectRatio;
}

void WebcamTex::draw(const glm::vec2 &size) {
    if (aspectRatio) {
        if (vidGrabber.getWidth() > vidGrabber.getHeight()) {
            vidGrabber.draw(0, 0, size.x, size.x/vidGrabber.getWidth() * vidGrabber.getHeight());
        }
        else {
            vidGrabber.draw(0, 0, size.y/vidGrabber.getHeight() * vidGrabber.getWidth(), size.y);
        }
    }
    else {
        vidGrabber.draw(0, 0, size.x, size.y);
    }
}

void WebcamTex::choose() {
    path = ofToString(random());
}
