#include "Tex.h"
#include "Layer.h"

ofFloatColor Tex::getTint(Layer *layer) const {
   const ofFloatColor& tint = layer->getColor();
   if (tint == ofFloatColor(0, 0)) {
       return ofFloatColor(1);
   }
   return tint;
}
