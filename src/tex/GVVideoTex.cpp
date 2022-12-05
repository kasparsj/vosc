#include "GVVideoTex.h"
#include "Layer.h"
#include "Console.h"

vector<string> loadLocalGVs()
{
    ofDirectory dir("videos");
    vector<string> videos;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        if (file.getExtension() == "gv") {
            videos.push_back(file.getFileName());
        }
    }
    return videos;
}

vector<string> GVVideoTex::cache = loadLocalGVs();

string GVVideoTex::random() {
    return cache[int(ofRandom(cache.size()))];
}

void GVVideoTex::update(TexData& data) {
    if (!_gpuVideo.isLoaded()) {
        string absPath = path;
        if (!ofFilePath::isAbsolute(absPath)) {
            absPath = ofToDataPath("videos/" + path);
            if (!ofFile(absPath).exists()) {
                absPath = ofToDataPath(absPath);
            }
        }
        if (_gpuVideo.load(absPath, ofxExtremeGpuVideo::GPU_VIDEO_STREAMING_FROM_STORAGE)) {
            _gv.load(absPath, ofxGvTexture::GPU_VIDEO_STREAMING_FROM_STORAGE);
            seek(data.timePct);
            isPlaying = true;
            prevPath = path;
            data.setSize(_gpuVideo.getWidth(), _gpuVideo.getHeight());
        }
        else {
            Console::getInstance().error("could not load hpv: " + path);
            path = prevPath;
            return;
        }
    }
    else if (data.getVar("onset")) {
        seek(data.timePct);
    }
    else if (isPlaying) {
        _gpuVideo.setTime(data.timePct * _gpuVideo.getDuration() + fmodf(ofGetElapsedTimef()-startTime, _gpuVideo.getDuration()));
        _gv.setTime(data.timePct * _gpuVideo.getDuration() + fmodf(ofGetElapsedTimef()-startTime, _gv.getDuration()));
    }
    aspectRatio = data.aspectRatio;
    _gpuVideo.update();
    _gv.update();
}

void GVVideoTex::seek(float pct) {
    _gpuVideo.setTime(_gpuVideo.getDuration() * pct);
    _gv.setTime(_gpuVideo.getDuration() * pct);
    startTime = ofGetElapsedTimef();
}

void GVVideoTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (aspectRatio) {
        ofPushMatrix();
        if (_gv.getWidth() > _gv.getHeight()) {
            ofTranslate(0, (size.y - size.x/_gv.getWidth() * _gv.getHeight()) / 2);
            _gv.getTexture().draw(pos.x, pos.y, size.x, size.x/_gv.getWidth() * _gv.getHeight());
        }
        else {
            ofTranslate((size.x - size.y/_gv.getHeight() * _gv.getWidth()) / 2, 0);
            _gv.getTexture().draw(pos.x, pos.y, size.y/_gv.getHeight() * _gv.getWidth(), size.y);
        }
        ofPopMatrix();
    }
    else {
        _gv.getTexture().draw(pos.x, pos.y, size.x, size.y);
    }
}

void GVVideoTex::choose() {
    path = random();
}
