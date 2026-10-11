// ItemModelGenerator.h -- vanilla ItemModelGenerator: item/generated layers
// become a front and back face plus side faces along the edges of the opaque
// texels, between z 7.5 and 8.5 (the "extruded" look of items in hand).
#pragma once

#include <string>
#include <vector>

#include "core/Image.h"
#include "resources/Model.h"

namespace mc {

// Elements for layer `layer` (tint index = layer) whose texture variable is
// `textureVar` ("layer0"); `frames` are the sprite's animation frames (their
// transparency is merged).
std::vector<ElementDef> generateLayerElements(int layer, const std::string& textureVar,
                                              const std::vector<const Image*>& frames);

} // namespace mc
