#include "FBOTex.h"

void FBOTex::clear() {
    fbo.begin();
    ofClear(0, 0, 0, 0);
    fbo.end();
}
