#include "HPVideoTex.h"
#include "Layer.h"

vector<string> loadLocalHPVs()
{
    ofDirectory dir("videos");
    vector<string> videos;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        if (file.getExtension() == "hpv") {
            videos.push_back(file.getFileName());
        }
    }
    return videos;
}

vector<string> HPVideoTex::cache = loadLocalHPVs();

bool HPVideoTex::engineInitialized = false;

string HPVideoTex::random() {
    return cache[int(ofRandom(cache.size()))];
}

void HPVideoTex::update(TexData& data) {
    if (!hpvPlayer.isLoaded()) {
        string absPath = path;
        if (!ofFilePath::isAbsolute(absPath)) {
            absPath = ofToDataPath("videos/" + path);
            if (!ofFile(absPath).exists()) {
                absPath = ofToDataPath(absPath);
            }
        }
        if (hpvPlayer.load(absPath)) {
            //hpvPlayer.setVolume(0);
            hpvPlayer.setLoopState(OF_LOOP_NORMAL);
            seek(data.timePct);
            hpvPlayer.play();
            prevPath = path;
            data.setSize(hpvPlayer.getWidth(), hpvPlayer.getHeight());
        }
        else {
            ofLog() << "could not load hpv: " << path;
            path = prevPath;
            return;
        }
    }
    else if (data.onset) {
        seek(data.timePct);
    }
    aspectRatio = data.aspectRatio;
}

void HPVideoTex::seek(float pct) {
    hpvPlayer.setPosition(pct);
}

void HPVideoTex::draw(const glm::vec2 &size) {
    if (aspectRatio) {
        if (hpvPlayer.getWidth() > hpvPlayer.getHeight()) {
            hpvPlayer.draw(0, 0, size.x, size.x/hpvPlayer.getWidth() * hpvPlayer.getHeight());
        }
        else {
            hpvPlayer.draw(0, 0, size.y/hpvPlayer.getHeight() * hpvPlayer.getWidth(), size.y);
        }
    }
    else {
        hpvPlayer.draw(0, 0, size.x, size.y);
    }
}

void HPVideoTex::choose() {
    path = random();
}
