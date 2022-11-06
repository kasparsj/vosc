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

void HPVideoTex::update(Layer *layer) {
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
            seek(layer->timePct);
            hpvPlayer.play();
            prevPath = path;
            layer->randomSeed = ofRandom(1000);
            if (layer->color == ofFloatColor(0, 0)) {
                layer->color = ofFloatColor(1);
            }
        }
        else {
            ofLog() << "could not load hpv: " << path;
            path = prevPath;
            return;
        }
    }
    else if (layer->data.onset) {
        seek(layer->timePct);
    }
    aspectRatio = layer->aspectRatio;
}

void HPVideoTex::seek(float pct) {
    hpvPlayer.setPosition(pct);
}

void HPVideoTex::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    if (aspectRatio) {
        if (hpvPlayer.getWidth() > hpvPlayer.getHeight()) {
            hpvPlayer.draw(pos.x, pos.y, size.x, size.x/hpvPlayer.getWidth() * hpvPlayer.getHeight());
        }
        else {
            hpvPlayer.draw(pos.x, pos.y, size.y/hpvPlayer.getHeight() * hpvPlayer.getWidth(), size.y);
        }
    }
    else {
        hpvPlayer.draw(pos.x, pos.y, size.x, size.y);
    }
}

void HPVideoTex::choose() {
    path = random();
}
