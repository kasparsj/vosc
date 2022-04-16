#include "Video.h"
#include "Layer.h"

vector<string> loadLocalVideos()
{
    ofDirectory dir("videos");
    vector<string> videos;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        videos.push_back(file.getFileName());
    }
    return videos;
}

vector<string> Video::cache = loadLocalVideos();

string Video::random() {
    return cache[int(ofRandom(cache.size()))];
}

void Video::update(Layer *layer) {
    if (!videoPlayer.isLoaded()) {
        videoPlayer.close();
        string absPath = path;
        if (!ofFilePath::isAbsolute(absPath)) {
            absPath = ofToDataPath("videos/" + path);
            if (!ofFile(absPath).exists()) {
                absPath = ofToDataPath(absPath);
            }
        }
        if (videoPlayer.load(absPath)) {
            videoPlayer.setVolume(0);
            videoPlayer.setLoopState(OF_LOOP_NORMAL);
            seek(layer->timeNorm);
            videoPlayer.play();
            prevPath = path;
            layer->randomSeed = ofRandom(1000);
        }
        else {
            ofLog() << "could not load video: " << path;
            path = prevPath;
            return;
        }
    }
    else if (layer->data->onset) {
        seek(layer->timeNorm);
    }
    aspectRatio = layer->aspectRatio;
    videoPlayer.update();
}

void Video::seek(float pos) {
    videoPlayer.setPosition(pos);
}

void Video::draw(const glm::vec3 &pos, const glm::vec3 &size) {
    if (aspectRatio) {
        if (videoPlayer.getWidth() > videoPlayer.getHeight()) {
            videoPlayer.draw(pos - size/2.f, size.x, size.x/videoPlayer.getWidth() * videoPlayer.getHeight());
        }
        else {
            videoPlayer.draw(pos - size/2.f, size.y/videoPlayer.getHeight() * videoPlayer.getWidth(), size.y);
        }
    }
    else {
        videoPlayer.draw(pos - size/2.f, size.x, size.y);
    }
}

void Video::choose() {
    path = random();
}

