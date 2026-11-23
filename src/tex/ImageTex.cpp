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

string ImageTex::random() {
    static const auto cache = loadLocalImages();
    if (cache.empty()) return {};
    return cache[int(ofRandom(cache.size()))];
}

bool ImageTex::loadImage() {
    string absPath = path;
    const bool remote = path.rfind("http://", 0) == 0 || path.rfind("https://", 0) == 0;
    if (!remote && !ofFilePath::isAbsolute(absPath)) {
        absPath = ofToDataPath("images/" + path);
        if (!ofFile(absPath).exists()) {
            absPath = ofToDataPath(absPath);
        }
    }
    if (remote ? image.load(absPath) : image.load(ofBufferFromFile(absPath, true))) {
        prevPath = path;
        return true;
    }
    ofLogError() << ("could not load image: " + path);
    return false;
}

void ImageTex::update(TexData& data) {
    if (isStatic && !needsUpdate) return;

    if (!image.isAllocated() && !loadImage()) {
        path = prevPath;
        return;
    }
    if (needsUpdate) {
        if (!data.hasExplicitSize()) data.setSize(image.getWidth(), image.getHeight());
        isStatic = true;
        needsUpdate = false;
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
