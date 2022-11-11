#include "LayerData.h"
#include "Layer.h"

void LayerData::update(const vector<Sound> &sounds, const vector<TidalNote> &notes, const map<string, VarConfig>& maps) {
    TexData::update(sounds, notes, maps);
    Layer* layer = dynamic_cast<Layer*>(parent);
    if (layer->onsetThresh.scale != 0) {
        onset = getValue(sounds, "__onsetThresh", layer->onsetThresh) > 1.f;
        // trigger onset once - only when crossing thresh going up
        if (onset && prevOnset) {
            onset = false;
            prevOnset = true;
        }
        else {
            prevOnset = onset;
        }
    }
    visible = onset || (layer->visibleThresh.scale == 0 || getValue(sounds, "__visibleThresh", layer->visibleThresh) >= 1.f);
}
