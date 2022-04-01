#include "FBOGen.h"

void FBOGen::clear() {
    fbo.begin();
    ofClear(0, 0, 0, 0);
    fbo.end();
}
