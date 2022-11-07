#include "ImageTex.h"
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

vector<string> ImageTex::cache = loadLocalImages();

string ImageTex::random() {
    return cache[int(ofRandom(cache.size()))];
}

void ImageTex::update(Layer *layer, Texture* tex) {
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
            if (layer->color == ofFloatColor(0, 0)) {
                layer->color = ofFloatColor(1);
            }
        }
        else {
            ofLog() << "could not load image: " << path;
            path = prevPath;
            return;
        }
    }
    aspectRatio = layer->aspectRatio;
}

void ImageTex::draw(const glm::vec2 &size) {
    if (aspectRatio) {
        if (image.getWidth() > image.getHeight()) {
            image.draw(0, 0, size.x, size.x/image.getWidth() * image.getHeight());
        }
        else {
            image.draw(0, 0, size.y/image.getHeight() * image.getWidth(), size.y);
        }
    }
    else {
        image.draw(0, 0, size.x, size.y);
    }
}

void ImageTex::choose() {
    path = random();
}
