#include "Image.h"
#include "Layer.h"

vector<string> loadLocalImages()
{
    ofDirectory dir("images");
    vector<string> images;
    for (int i = 0; i < dir.getFiles().size(); i++){
        ofFile file = dir.getFile(i);
        if (file.getExtension() == "png" || file.getExtension() == "jpg" || file.getExtension() == "jpeg") {
            images.push_back(file.getFileName());
        }
    }
    return images;
}

vector<string> Image::cache = loadLocalImages();

string Image::random() {
    return cache[int(ofRandom(cache.size()))];
}

void Image::update(Layer *layer) {
    if (!image.isAllocated()) {
        image.load("images/" + path);
    }
    maintainAspectRatio = !layer->freeRatio;
}

void Image::draw(int left, int top, int width, int height) {
    if (maintainAspectRatio) {
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
