#include "HPVideo.h"
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

vector<string> HPVideo::cache = loadLocalHPVs();

string HPVideo::random() {
    return cache[int(ofRandom(cache.size()))];
}

void HPVideo::update(Layer *layer) {
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
    else if (layer->data->onset) {
        seek(layer->timePct);
    }
    aspectRatio = layer->aspectRatio;
}

void HPVideo::seek(float pct) {
    hpvPlayer.setPosition(pct);
}

void HPVideo::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    const glm::vec3 posCenter = pos - size/2.f;
    if (aspectRatio) {
        if (hpvPlayer.getWidth() > hpvPlayer.getHeight()) {
            hpvPlayer.draw(posCenter.x, posCenter.y, size.x, size.x/hpvPlayer.getWidth() * hpvPlayer.getHeight());
        }
        else {
            hpvPlayer.draw(posCenter.x, posCenter.y, size.y/hpvPlayer.getHeight() * hpvPlayer.getWidth(), size.y);
        }
    }
    else {
        hpvPlayer.draw(posCenter.x, posCenter.y, size.x, size.y);
    }
}

void HPVideo::choose() {
    path = random();
}

ofFloatColor HPVideo::getTint(Layer *layer) {
    return layer->getColor();
}
