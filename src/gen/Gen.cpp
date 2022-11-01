#include "Gen.h"
#include "Layer.h"

ofFloatColor Gen::getTint(Layer *layer) const {
   const ofFloatColor& tint = layer->getColor();
   if (tint == ofFloatColor(0, 0)) {
       return ofFloatColor(1);
   }
   return tint;
}
