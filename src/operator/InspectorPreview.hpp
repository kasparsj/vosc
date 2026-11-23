#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI
#include <cstddef>
#include <memory>
#include <vector>

class Buffer;
class Geom;
class OSCInput;
class Texture;
class ofTexture;

namespace Rml { class Element; }

namespace vosc::operator_ui {

struct InspectorPreviewSlot {
    std::weak_ptr<Texture> texture;
    std::weak_ptr<Buffer> buffer;
    std::weak_ptr<Geom> geometry;
    std::weak_ptr<OSCInput> input;
    int slice = 0;
};

const ofTexture* inspectorTextureOutput(const Texture& texture);
int inspectorArrayDepth(const ofTexture& output);
bool inspectorCanPreviewTexture(const ofTexture& output);
std::size_t inspectorBufferTexels(const Buffer& buffer);

class InspectorPreview {
public:
    InspectorPreview();
    ~InspectorPreview();

    void paint(Rml::Element& content, const std::vector<InspectorPreviewSlot>& slots, const int* viewport);

private:
    struct Renderer;
    std::unique_ptr<Renderer> renderer;
};

} // namespace vosc::operator_ui
#endif
