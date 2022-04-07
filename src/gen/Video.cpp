#include "Video.h"
#include "Layer.h"

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

string Video::random() {
    return videos[int(ofRandom(videos.size()))];
}

void Video::update(Layer *layer) {
    if (!videoPlayer.isLoaded()) {
        videoPlayer.close();
        videoPlayer.load("videos/" + name + ".mov");
        videoPlayer.setVolume(0);
        videoPlayer.setLoopState(OF_LOOP_NORMAL);
        seek(layer->timeNorm);
        videoPlayer.play();
    }
    else if (layer->data->onset) {
        seek(layer->timeNorm);
    }
    videoPlayer.update();
}

void Video::seek(float pos) {
    videoPlayer.setPosition(pos);
}

void Video::draw(int left, int top, int width, int height) {
    videoPlayer.draw(left, top, width, height);
}

void Video::choose() {
    name = random();
}

