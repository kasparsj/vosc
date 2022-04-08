#include "Image.h"
#include "Layer.h"

vector<string> loadLocalImages()
{
    ofDirectory dir("images");
    vector<string> images;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        images.push_back(file.getFileName());
    }
    return images;
}

vector<string> Image::cache = loadLocalImages();

string Image::random() {
    return cache[int(ofRandom(cache.size()))];
}

void Image::update(Layer *layer) {
    if (!image.isAllocated()) {
        string absPath = path;
        if (absPath.substr(0, 7) != "http://" && absPath.substr(0, 8) != "https://" && !ofFilePath::isAbsolute(absPath)) {
            absPath = ofToDataPath("images/" + path);
            if (!ofFile(absPath).exists()) {
                absPath = ofToDataPath(absPath);
            }
        }
        if (image.load(absPath)) {
            prevPath = path;
            layer->randomSeed = ofRandom(1000);
        }
        else {
            ofLog() << "could not load image: " << path;
            path = prevPath;
            return;
        }
    }
    aspectRatio = layer->aspectRatio;
}

void Image::draw(int left, int top, int width, int height) {
    if (aspectRatio) {
        if (image.getWidth() > image.getHeight()) {
            image.draw(left, top, width, width/image.getWidth() * image.getHeight());
        }
        else {
            image.draw(left, top, height/image.getHeight() * image.getWidth(), height);
        }
    }
    else {
        image.draw(left, top, width, height);
    }
}

void Image::choose() {
    path = random();
}
