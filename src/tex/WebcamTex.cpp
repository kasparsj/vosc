#include "WebcamTex.h"
#include "Layer.h"

#ifndef TARGET_OSX

WebcamTex::WebcamTex(string path, const vector<float>& args) : BaseTex(path, args) {}
WebcamTex::~WebcamTex() { vidGrabber.close(); }

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
    aspectRatio = data.aspectRatio;
    if (!vidGrabber.isInitialized()) {
        observedFrame = false;
        startupError.clear();
        vidGrabber.setDeviceID(stoi(path));
        if (!vidGrabber.setup(args.size() > 0 ? args[0] : 1920,
                              args.size() > 1 ? args[1] : 1080)) {
            startupError = "could not initialize camera device " + path +
                "; check device availability and Camera permission";
            return;
        }
        data.setSize(vidGrabber.getWidth(), vidGrabber.getHeight());
    }
    vidGrabber.update();
    if (!observedFrame && vidGrabber.isFrameNew() && vidGrabber.isInitialized() &&
        vidGrabber.getTexture().isAllocated() && vidGrabber.getWidth() > 0 &&
        vidGrabber.getHeight() > 0) {
        observedFrame = true;
        if (!data.hasExplicitSize()) {
            const auto& frame = vidGrabber.getTexture();
            if (frame.getWidth() != data.getSize().x || frame.getHeight() != data.getSize().y) {
                data.setSize(frame.getWidth(), frame.getHeight());
            }
        }
    }
}

bool WebcamTex::startupFrameReady() const {
    return observedFrame && vidGrabber.isInitialized() && vidGrabber.getTexture().isAllocated();
}

void WebcamTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (!vidGrabber.isInitialized() || vidGrabber.getWidth() <= 0 || vidGrabber.getHeight() <= 0) return;
    if (aspectRatio) {
        if (vidGrabber.getWidth() > vidGrabber.getHeight()) {
            vidGrabber.draw(pos, size.x, size.x/vidGrabber.getWidth() * vidGrabber.getHeight());
        }
        else {
            vidGrabber.draw(pos, size.y/vidGrabber.getHeight() * vidGrabber.getWidth(), size.y);
        }
    }
    else {
        vidGrabber.draw(pos, size.x, size.y);
    }
}

void WebcamTex::choose() {
    path = ofToString(random());
}

bool WebcamTex::isFrameNew() { return vidGrabber.isFrameNew(); }
ofTexture& WebcamTex::getTexture() { return vidGrabber.getTexture(); }
const ofTexture& WebcamTex::getTexture() const { return vidGrabber.getTexture(); }
void WebcamTex::setUseTexture(bool enabled) { vidGrabber.setUseTexture(enabled); }
bool WebcamTex::isUsingTexture() const { return vidGrabber.isUsingTexture(); }
ofPixels& WebcamTex::getPixels() { return vidGrabber.getPixels(); }

#endif
