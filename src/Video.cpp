#include "Video.h"

vector<string> loadVideos()
{
    ofDirectory dir("videos");
    vector<string> videos;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        if (file.getExtension() == "mov") {
            string fileName = file.getFileName();
            videos.push_back(fileName.substr(0, fileName.find("." + file.getExtension())));
        }
    }
    return videos;
}

vector<string> Video::videos = loadVideos();

void Video::update() {
    Source::update();
    if (!videoPlayer.isLoaded()) {
        videoPlayer.close();
        videoPlayer.load("videos/" + name + ".mov");
        videoPlayer.setVolume(0);
        videoPlayer.setLoopState(OF_LOOP_NORMAL);
        resetPos();
        videoPlayer.play();
    }
    videoPlayer.update();
}

void Video::resetPos() {
    videoPlayer.setPosition(pos);
}

void Video::draw(int left, int top, int width, int height) {
    videoPlayer.draw(left, top, width, height);
}

void Video::random() {
    name = videos[int(ofRandom(videos.size()))];
}

