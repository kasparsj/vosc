#include "LayerData.h"
#include "Layer.h"

void LayerData::update(const vector<Sound> &sounds, const vector<TidalNote> &notes) {
    TexData::update(sounds, notes);
    Layer* layer = dynamic_cast<Layer*>(parent);
    // todo: fix
//    if (layer->getVar("onsetThresh").scale != 0) {
//        onset = getValue(sounds, "__onsetThresh", layer->onsetThresh) > 1.f;
//        // trigger onset once - only when crossing thresh going up
//        if (onset && prevOnset) {
//            onset = false;
//            prevOnset = true;
//        }
//        else {
//            prevOnset = onset;
//        }
//    }
//    visible = onset || (layer->getVar("visibleThresh").scale == 0 || getValue(sounds, "__visibleThresh", layer->visibleThresh) >= 1.f);
    visible = true;
}
