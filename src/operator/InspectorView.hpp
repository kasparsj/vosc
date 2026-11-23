#pragma once

#include "../Config.h"

#if VOSC_OPERATOR_UI

#include "InspectorPreview.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class BaseVar;
class Buffer;
class Geom;
class Layer;
class OSCInput;
class Shader;
class Texture;
class VOSC;

namespace vosc::operator_ui {

class InspectorView {
public:
    struct Navigation {
        std::string focus;
        float scroll = 0;
        uint64_t origin = 0;
        bool toOverview = false;
    };

    void reset();
    void beginMarkup();
    void endMarkup();
    std::string layerLink(const Layer& layer, const VOSC& renderer, bool diagnostic = false);
    std::string markup(const VOSC& renderer, const std::string& shaderDiagnostic);
    std::string title() const;
    std::string toolbar() const;
    void syncReadings(Rml::Element& content, bool layoutChanged);
    const auto& previews() const { return previewSlots; }
    std::optional<Navigation> activate(VOSC& renderer, const std::string& id, uint64_t token,
                                       float scroll, bool fromOverview);
    bool matches(const std::string& id, uint64_t token) const;
    bool detail() const { return !pages.empty(); }
    bool expanded() const;
    bool isLayerShader() const;
    int height(const VOSC& renderer, bool compact) const;
    std::shared_ptr<Layer> selectedLayer(const VOSC& renderer) const;

private:
    enum class Kind { Layer, Shader, Texture, Buffer, SharedTexture, SharedShader, Geometry, Input, Preview };
    struct Key {
        Kind kind;
        std::string name;
        int layer = -1;
    };
    struct Identity {
        std::weak_ptr<const void> owner;
        std::weak_ptr<const void> backend;
        unsigned program = 0;
        uint64_t sourceRevision = 0;
        std::string source;
        bool operator==(const Identity& other) const;
    };
    struct Resource {
        Identity identity;
        std::shared_ptr<Layer> layer;
        std::shared_ptr<const Shader> shader;
        std::shared_ptr<Texture> texture;
        std::shared_ptr<Buffer> buffer;
        std::shared_ptr<Geom> geometry;
        std::shared_ptr<OSCInput> input;
    };
    struct Item {
        Key key;
        Identity identity;
    };
    struct Row {
        Item item;
        uint64_t token;
    };
    struct Page {
        Item item;
        Navigation back;
        Row origin;
        std::string section = "bindings";
        bool sampling = false;
        bool passes = false;
        bool variables = false;
        bool choosingSlice = false;
        int slice = 0;
    };
    enum class State { Live, Changed, Unavailable };
    struct Resolved {
        State state = State::Live;
        size_t live = 0;
        Resource resource;
    };

    static Resource resolve(const VOSC& renderer, const Key& key, const Resource& parent);
    Resolved resolvePages(const VOSC& renderer);
    static std::string rowId(const Key& key, bool rail = false);
    std::string row(const Key& key, const Resource& current, const std::string& label,
                    const std::string& subtitle = {}, const std::string& kind = {}, bool rail = false);
    std::string bindings(const VOSC& renderer, const Resource& shader, bool fromLayer);
    std::string globalsMarkup(const VOSC& renderer);
    std::string shaderMarkup(const VOSC& renderer, const Resource& resource,
                             const std::string& diagnostic);
    std::string textureMarkup(const VOSC& renderer, const Resource& resource);
    std::string auxiliaryMarkup(const Resource& resource);
    std::string preview(const Resource& resource, bool thumbnail, int slice = 0);
    std::string reading(std::string value);
    std::string valueRow(const std::string& name, const std::string& type, std::string value);
    std::string variableRows(const std::map<std::string, std::shared_ptr<BaseVar>>& variables);
    std::vector<InspectorPreviewSlot> previewSlots;
    std::vector<std::string> nextReadings, renderedReadings;
    std::vector<Page> pages;
    std::map<std::string, Row> visibleRows, nextRows;
    std::optional<std::pair<std::string, Row>> restoreRow;
    State invalid = State::Live;
    size_t firstInvalid = 0;
    uint64_t nextToken = 1;
    bool globals = false;
};

} // namespace vosc::operator_ui
#endif
