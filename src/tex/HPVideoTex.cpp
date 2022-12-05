#include "HPVideoTex.h"
#include "Layer.h"
#include "Console.h"

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
            Console::get().error("could not load hpv: " + path);
            path = prevPath;
            return;
        }
    }
    else if (data.getVar("onset")) {
        seek(data.timePct);
    }
    aspectRatio = data.aspectRatio;
}

void HPVideoTex::seek(float pct) {
    hpvPlayer.setPosition(pct);
}

void HPVideoTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (aspectRatio) {
        ofPushMatrix();
        if (hpvPlayer.getWidth() > hpvPlayer.getHeight()) {
            ofTranslate(0, (size.y - size.x/hpvPlayer.getWidth() * hpvPlayer.getHeight()) / 2);
            hpvPlayer.draw(pos.x, pos.y, size.x, size.x/hpvPlayer.getWidth() * hpvPlayer.getHeight());
        }
        else {
            ofTranslate((size.x - size.y/hpvPlayer.getHeight() * hpvPlayer.getWidth()) / 2, 0);
            hpvPlayer.draw(pos.x, pos.y, size.y/hpvPlayer.getHeight() * hpvPlayer.getWidth(), size.y);
        }
        ofPopMatrix();
    }
    else {
        hpvPlayer.draw(pos.x, pos.y, size.x, size.y);
    }
}

void HPVideoTex::choose() {
    path = random();
}
