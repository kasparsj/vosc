#include "Button.h"

Button::Button(float x, float y, float width, float height)
    : x(x), y(y), width(width), height(height), fill(false), color(255), labelColor(255) {
}

void Button::setPosition(float x, float y) {
    this->x = x;
    this->y = y;
}

void Button::setSize(float width, float height) {
    this->width = width;
    this->height = height;
}

void Button::setBounds(float x, float y, float width, float height) {
    this->x = x;
    this->y = y;
    this->width = width;
    this->height = height;
}

bool Button::hitTest(float x, float y) const {
    return (x >= this->x && x <= this->x + this->width &&
            y >= this->y && y <= this->y + this->height);
}

void Button::draw() const {
    ofPushStyle();
    if (fill) {
        ofFill();
    } else {
        ofNoFill();
    }
    ofSetColor(color);
    ofDrawRectangle(x, y, width, height);
    ofPopStyle();
}

void Button::draw(const string& label) const {
    draw();
    ofPushStyle();
    ofSetColor(labelColor);
    ofDrawBitmapString(label, x, y + height / 2 + 4);
    ofPopStyle();
}

