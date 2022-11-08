#include "VideoTex.h"
#include "Layer.h"

vector<string> loadLocalVideos()
{
    ofDirectory dir("videos");
    vector<string> videos;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        if (file.getExtension() == "mov") {
            videos.push_back(file.getFileName());
        }
    }
    return videos;
}

vector<string> VideoTex::cache = loadLocalVideos();

string VideoTex::random() {
    return cache[int(ofRandom(cache.size()))];
}

void VideoTex::update(TexData& data) {
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
            seek(data.timePct);
            videoPlayer.play();
            prevPath = path;
            data.setSize(videoPlayer.getWidth(), videoPlayer.getHeight());
        }
        else {
            ofLog() << "could not load video: " << path;
            path = prevPath;
            return;
        }
    }
    else if (data.onset) {
        seek(data.timePct);
    }
    aspectRatio = data.aspectRatio;
    videoPlayer.update();
}

void VideoTex::seek(float pct) {
    videoPlayer.setPosition(pct);
}

void VideoTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (aspectRatio) {
        if (videoPlayer.getWidth() > videoPlayer.getHeight()) {
            videoPlayer.draw(pos, size.x, size.x/videoPlayer.getWidth() * videoPlayer.getHeight());
        }
        else {
            videoPlayer.draw(pos, size.y/videoPlayer.getHeight() * videoPlayer.getWidth(), size.y);
        }
    }
    else {
        videoPlayer.draw(pos, size.x, size.y);
    }
}

void VideoTex::choose() {
    path = random();
}
