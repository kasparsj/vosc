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
    if (cache.size() > 0) {
        return cache[int(ofRandom(cache.size()))];
    }
    return "";
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
            videoPlayer.setSpeed(data.getVar("speed"));
            seek(data.getVarPercent("timePct"));
            videoPlayer.play();
            prevPath = path;
            data.setSize(videoPlayer.getWidth(), videoPlayer.getHeight());
        }
        else {
            ofLogError() << ("could not load video: " + path);
            path = prevPath;
            return;
        }
    }
    else if (data.getVar("onset")) {
        seek(data.getVarPercent("timePct"));
    }
    aspectRatio = data.aspectRatio;
    videoPlayer.update();
}

void VideoTex::seek(float pct) {
    videoPlayer.setPosition(pct);
}

void VideoTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (aspectRatio) {
        ofPushMatrix();
        if (videoPlayer.getWidth() > videoPlayer.getHeight()) {
            ofTranslate(0, (size.y - size.x/videoPlayer.getWidth() * videoPlayer.getHeight()) / 2);
            videoPlayer.draw(pos, size.x, size.x/videoPlayer.getWidth() * videoPlayer.getHeight());
        }
        else {
            ofTranslate((size.x - size.y/videoPlayer.getHeight() * videoPlayer.getWidth()) / 2, 0);
            videoPlayer.draw(pos, size.y/videoPlayer.getHeight() * videoPlayer.getWidth(), size.y);
        }
        ofPopMatrix();
    }
    else {
        videoPlayer.draw(pos, size.x, size.y);
    }
}

void VideoTex::choose() {
    path = random();
}
