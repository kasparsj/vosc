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

void ImageTex::update(TexData& data) {
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
            data.setSize(image.getWidth(), image.getHeight());
        }
        else {
            ofLog() << "could not load image: " << path;
            path = prevPath;
            return;
        }
    }
    aspectRatio = data.aspectRatio;
}

void ImageTex::draw(const glm::vec2 &pos, const glm::vec2 &size) {
    if (aspectRatio) {
        ofPushMatrix();
        if (image.getWidth() > image.getHeight()) {
            ofTranslate(0, (size.y - size.x/image.getWidth() * image.getHeight()) / 2.f);
            image.draw(pos, size.x, size.x/image.getWidth() * image.getHeight());
        }
        else {
            ofTranslate((size.x - size.y/image.getHeight() * image.getWidth()) / 2.f, 0);
            image.draw(pos, size.y/image.getHeight() * image.getWidth(), size.y);
        }
        ofPopMatrix();
    }
    else {
        image.draw(pos, size.x, size.y);
    }
}

void ImageTex::choose() {
    path = random();
}
