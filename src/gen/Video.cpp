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
        videoPlayer.load(absPath);
        videoPlayer.setVolume(0);
        videoPlayer.setLoopState(OF_LOOP_NORMAL);
        seek(layer->timeNorm);
        videoPlayer.play();
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

void Video::draw(int left, int top, int width, int height) {
    if (aspectRatio) {
        if (videoPlayer.getWidth() > videoPlayer.getHeight()) {
            videoPlayer.draw(left, top, width, width/videoPlayer.getWidth() * videoPlayer.getHeight());
        }
        else {
            videoPlayer.draw(left, top, height/videoPlayer.getHeight() * videoPlayer.getWidth(), height);
        }
    }
    else {
        videoPlayer.draw(left, top, width, height);
    }
}

void Video::choose() {
    path = random();
}

