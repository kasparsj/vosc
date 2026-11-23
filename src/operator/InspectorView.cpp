#include "InspectorView.hpp"

#if VOSC_OPERATOR_UI
#include "../VOSC.hpp"
#include "../ResourceRegistry.hpp"
#include "../tex/ShaderTex.h"
#include <RmlUi/Core/StringUtilities.h>

namespace vosc::operator_ui {
namespace {

std::string encode(const std::string& text) {
    return Rml::StringUtilities::EncodeRml(text);
}

std::string idPart(const std::string& text) {
    static constexpr char hex[] = "0123456789abcdef";
    std::string id;
    id.reserve(text.size() * 2);
    for (unsigned char c : text) {
        id.push_back(hex[c >> 4]);
        id.push_back(hex[c & 15]);
    }
    return id;
}

std::string heading(const std::string& text) {
    return "<div class='inspect-heading'>" + encode(text) + "</div>";
}

std::string label(const std::string& text) {
    return "<div class='inspect-label'>" + encode(text) + "</div>";
}

std::string quiet(const std::string& text) {
    return "<div class='caption'>" + encode(text) + "</div>";
}

std::string control(const std::string& id, const std::string& text, bool selected = false) {
    return "<button id='" + id + "'" + (selected ? " class='selected'" : "") + ">"
        + encode(text) + "</button>";
}

template<typename Map>
typename Map::mapped_type find(const Map& pool, const std::string& name) {
    const auto it = pool.find(name);
    return it != pool.end() ? it->second : nullptr;
}

std::string shaderDescription(const Shader& shader) {
    if (!shader.isLoaded()) return "No shader loaded";
    return shader.isShaderLoaded() ? "Standard GLSL · linked" : "Shadertoy · loaded";
}

std::string sourceName(const Shader& shader) {
    const auto path = shader.getShaderPath();
    if (!path.empty()) return path;
    return shader.isLoaded() ? "Shader source unavailable" : "Shader";
}

std::string textureSource(const Texture& texture) {
    return texture.tex && !texture.tex->path.empty() ? texture.tex->path : "Source unavailable";
}

bool sameOwner(const std::weak_ptr<const void>& a, const std::weak_ptr<const void>& b) {
    return !a.owner_before(b) && !b.owner_before(a);
}

} // namespace

bool InspectorView::Identity::operator==(const Identity& other) const {
    return sameOwner(owner, other.owner) && sameOwner(backend, other.backend) &&
        program == other.program && source == other.source;
}

InspectorView::Resource InspectorView::resolve(const VOSC& renderer, const Key& key, const Resource& parent) {
    Resource result;
    const auto* registry = ResourceRegistry::current();
    const auto& layers = renderer.operatorLayers();
    switch (key.kind) {
        case Kind::Layer:
            if (key.layer < 0 || static_cast<size_t>(key.layer) >= layers.size()) break;
            if (!layers[key.layer] || layers[key.layer]->index != key.layer) break;
            result.layer = layers[key.layer];
            result.identity.owner = result.layer;
            break;
        case Kind::Shader:
            if (parent.layer)
                result.shader = std::shared_ptr<const Shader>(parent.layer, &parent.layer->shader);
            else if (parent.texture)
                result.shader = std::dynamic_pointer_cast<const ShaderTex>(parent.texture->tex);
            break;
        case Kind::SharedShader:
            if (registry) result.shader = find(registry->shaderSharedPool, key.name);
            break;
        case Kind::Texture:
            if (parent.shader) result.texture = find(parent.shader->getTextures(), key.name);
            else if (parent.layer) result.texture = find(parent.layer->shader.getTextures(), key.name);
            break;
        case Kind::SharedTexture:
            if (registry) result.texture = find(registry->textureSharedPool, key.name);
            break;
        case Kind::Buffer:
            if (parent.shader) result.buffer = find(parent.shader->getBuffers(), key.name);
            else if (parent.layer) result.buffer = find(parent.layer->shader.getBuffers(), key.name);
            if (result.buffer) result.identity.owner = result.buffer;
            break;
        case Kind::Geometry: {
            auto layer = parent.layer;
            if (!layer) layer = resolve(renderer, {Kind::Layer, {}, key.layer}, {}).layer;
            if (layer) {
                result.geometry = layer->geom;
                result.identity.owner = result.geometry;
                result.identity.backend = layer;
            }
            break;
        }
        case Kind::Input:
            result.input = find(Inputs::get().all(), key.name);
            result.identity.owner = result.input;
            break;
        case Kind::Preview:
            return parent;
    }
    if (result.shader) {
        result.identity.owner = result.shader;
        result.identity.backend = result.shader->operatorBackend();
        result.identity.program = result.shader->operatorLinkedProgram();
        result.identity.source = result.shader->getShaderPath();
    }
    if (result.texture) {
        result.identity.owner = result.texture;
        result.identity.backend = result.texture->tex;
        result.identity.source = textureSource(*result.texture);
    }
    return result;
}

InspectorView::Resolved InspectorView::resolvePages(const VOSC& renderer) {
    Resolved result;
    for (const auto& page : pages) {
        if (invalid != State::Live && result.live == firstInvalid) {
            result.state = invalid;
            break;
        }
        auto current = resolve(renderer, page.item.key, result.resource);
        if (current.identity.owner.expired()) {
            result.state = State::Unavailable;
            break;
        }
        if (!(current.identity == page.item.identity)) {
            result.state = State::Changed;
            break;
        }
        result.resource = std::move(current);
        ++result.live;
    }
    if (result.state != State::Live) {
        invalid = result.state;
        firstInvalid = result.live;
    }
    return result;
}

void InspectorView::reset() {
    pages.clear();
    restoreRow.reset();
    invalid = State::Live;
}

void InspectorView::beginMarkup() {
    nextRows.clear();
}

void InspectorView::endMarkup() {
    visibleRows.swap(nextRows);
    nextRows.clear();
    restoreRow.reset();
}

bool InspectorView::matches(const std::string& id, uint64_t token) const {
    const auto row = visibleRows.find(id);
    return row != visibleRows.end() && row->second.token == token;
}

std::string InspectorView::row(const Key& key, const Resource& current, const std::string& text,
                              const std::string& subtitle, const std::string& kind, bool rail) {
    const auto id = std::string(rail ? "inspect-rail-" : "inspect-row-")
        + std::to_string(static_cast<int>(key.kind)) + "-" + std::to_string(key.layer) + "-" + idPart(key.name);
    const auto previous = visibleRows.find(id);
    uint64_t token;
    if (restoreRow && restoreRow->first == id && restoreRow->second.item.identity == current.identity)
        token = restoreRow->second.token;
    else if (previous != visibleRows.end() && previous->second.item.identity == current.identity)
        token = previous->second.token;
    else token = nextToken++;
    nextRows.insert_or_assign(id, Row{{key, current.identity}, token});
    return "<button class='inspect-row" + std::string(rail ? " inspect-rail-row" : "") + "' id='" + id
        + "' data-target='" + std::to_string(token) + "'><span class='inspect-row-label'><span>"
        + encode(text) + "</span>" + (subtitle.empty() ? "" : "<span class='inspect-subtitle'>" + encode(subtitle) + "</span>")
        + "</span><span class='inspect-kind'>" + encode(kind) + " &#8250;</span></button>";
}

std::string InspectorView::layerLink(const Layer& layer, const VOSC& renderer, bool diagnostic) {
    const Key key{Kind::Layer, {}, layer.index};
    const std::string title = (diagnostic ? "Inspect Layer " : "Layer ") + std::to_string(layer.index);
    return row(key, resolve(renderer, key, {}), title,
               diagnostic ? "" : sourceName(layer.shader), layer.reloadDiagnostic() ? "Reload failed" : "");
}

std::optional<InspectorView::Navigation> InspectorView::activate(
    VOSC& renderer, const std::string& id, uint64_t token, float scroll, bool fromOverview) {
    if (id == "back" && !pages.empty()) {
        const auto resolved = resolvePages(renderer);
        const auto destination = std::min(resolved.live, pages.size() - 1);
        auto back = pages[destination].back;
        restoreRow = std::make_pair(back.focus, pages[destination].origin);
        pages.erase(pages.begin() + destination, pages.end());
        invalid = State::Live;
        return back;
    }
    auto resolved = resolvePages(renderer);
    if (resolved.state != State::Live) return {};
    if (id == "inspect-layers" || id == "inspect-globals") {
        globals = id == "inspect-globals";
        return Navigation{id};
    }
    if (!matches(id, token)) return {};
    const auto& activated = visibleRows.at(id);
    const auto& item = activated.item;
    const bool rail = id.compare(0, 13, "inspect-rail-") == 0;
    const auto resource = resolve(renderer, item.key, rail ? Resource{} : resolved.resource);
    Navigation back{id, scroll, token, fromOverview};
    auto origin = activated;
    if (rail) {
        back = pages.front().back;
        origin = pages.front().origin;
        pages.clear();
    }
    // Preserve the presented identity even when it vanished after input.
    // The resulting detail reports the stale route instead of selecting its replacement.
    pages.push_back({item, back, std::move(origin)});
    std::string focus = "back";
    if (resource.identity == item.identity) {
        if (item.key.kind == Kind::Layer) focus = "inspect-row-1--1-";
        if (item.key.kind == Kind::Shader) {
            const auto layer = isLayerShader() ? selectedLayer(renderer) : nullptr;
            focus = layer && layer->reloadDiagnostic() ? "shader-diagnostic" : "inspect-bindings";
        }
    }
    return Navigation{focus};
}

std::string InspectorView::bindings(const VOSC& renderer, const Resource& resource, bool fromLayer) {
    const auto& shader = resource.layer ? resource.layer->shader : *resource.shader;
    std::string out;
    const auto& textures = shader.getTextures();
    const auto& buffers = shader.getBuffers();
    if (fromLayer) {
        out += heading("Default texture");
        const Key key{Kind::Texture, DEFAULT_TEX};
        const auto texture = resolve(renderer, key, resource);
        if (texture.texture) out += row(key, texture, "Default texture", textureSource(*texture.texture), "Texture");
        else out += quiet("No default texture");
    }
    size_t named = textures.size() - (fromLayer && textures.count(DEFAULT_TEX) ? 1 : 0);
    out += heading("Texture bindings · " + std::to_string(named));
    for (const auto& [name, texture] : textures) {
        if (!texture || (fromLayer && name == DEFAULT_TEX)) continue;
        const Key key{Kind::Texture, name};
        out += row(key, resolve(renderer, key, resource), name, textureSource(*texture), "Texture");
    }
    out += heading("Buffers · " + std::to_string(buffers.size()));
    for (const auto& [name, buffer] : buffers) {
        if (!buffer) continue;
        const Key key{Kind::Buffer, name};
        out += row(key, resolve(renderer, key, resource), name, {}, "Buffer");
    }
    return out;
}

std::string InspectorView::markup(const VOSC& renderer, const std::string& shaderDiagnostic) {
    const auto& layers = renderer.operatorLayers();
    if (pages.empty()) {
        std::string out = "<div class='inspect-tabs'>" + control("inspect-layers", "Layers", !globals)
            + control("inspect-globals", "Globals", globals) + "</div>";
        if (!globals) {
            out += heading("OSC layers · " + std::to_string(layers.size()));
            for (const auto& layer : layers) if (layer) out += layerLink(*layer, renderer);
        } else {
            out += heading("Shared textures");
            const auto* registry = ResourceRegistry::current();
            if (registry) {
                for (const auto& [name, texture] : registry->textureSharedPool) {
                    if (!texture) continue;
                    const Key key{Kind::SharedTexture, name};
                    out += row(key, resolve(renderer, key, {}), name, textureSource(*texture), "Texture");
                }
                out += heading("Pooled shaders");
                for (const auto& [name, shader] : registry->shaderSharedPool) {
                    if (!shader) continue;
                    const Key key{Kind::SharedShader, name};
                    out += row(key, resolve(renderer, key, {}), name, sourceName(*shader), "Shader");
                }
            }
        }
        return out;
    }

    const auto resolved = resolvePages(renderer);
    if (resolved.state != State::Live)
        return label(resolved.state == State::Changed ? "Resource changed" : "Resource unavailable")
            + quiet("Go back and select a current resource.");
    const auto& resource = resolved.resource;
    const auto& key = pages.back().item.key;
    if (resource.layer) {
        std::string rail = "<div class='inspect-rail'>";
        for (const auto& layer : layers) {
            if (!layer) continue;
            const Key layerKey{Kind::Layer, {}, layer->index};
            rail += row(layerKey, resolve(renderer, layerKey, {}), "Layer " + std::to_string(layer->index),
                        {}, {}, true);
        }
        const Key shaderKey{Kind::Shader, {}};
        return "<div class='inspect-layer-columns'>" + rail + "</div><div class='inspect-layer-body'>"
            + label("Layer " + std::to_string(resource.layer->index)) + heading("Shader")
            + row(shaderKey, resolve(renderer, shaderKey, resource), sourceName(resource.layer->shader),
                  shaderDescription(resource.layer->shader), resource.layer->reloadDiagnostic() ? "Reload failed" : "Shader")
            + bindings(renderer, resource, true) + "</div></div>";
    }
    if (resource.shader) {
        return "<div class='inspect-tabs'>" + control("inspect-bindings", "Bindings", true) + "</div>"
            + label(key.name.empty() ? sourceName(*resource.shader) : key.name)
            + quiet(shaderDescription(*resource.shader)) + shaderDiagnostic + bindings(renderer, resource, false);
    }
    if (resource.texture) {
        const auto name = key.name == DEFAULT_TEX && pages.size() > 1
            && pages[pages.size() - 2].item.key.kind == Kind::Layer ? "Default texture" : key.name;
        auto out = label(name) + quiet("Source · " + textureSource(*resource.texture));
        if (!resource.texture->isLoaded() || !resource.texture->hasTexture())
            return out + quiet("Texture output unavailable");
        const auto& output = resource.texture->getTexture();
        if (!output.isAllocated()) return out + quiet("Texture output unavailable");
        return out + quiet(std::to_string(static_cast<int>(output.getWidth())) + " × "
            + std::to_string(static_cast<int>(output.getHeight())) + " · Texture output");
    }
    if (resource.buffer)
        return label(key.name) + quiet("Buffer binding") + quiet(std::to_string(resource.buffer->buf.size()) + " bytes");
    return {};
}

std::shared_ptr<Layer> InspectorView::selectedLayer(const VOSC& renderer) const {
    if (pages.empty() || pages.front().item.key.kind != Kind::Layer) return {};
    auto layer = resolve(renderer, pages.front().item.key, {});
    return layer.identity == pages.front().item.identity ? layer.layer : nullptr;
}

std::string InspectorView::title() const {
    if (pages.empty()) return "Inspector";
    std::string prefix;
    if (pages.front().item.key.kind == Kind::Layer)
        prefix = "Layer " + std::to_string(pages.front().item.key.layer);
    const char* detail = "";
    switch (pages.back().item.key.kind) {
        case Kind::Layer: return prefix;
        case Kind::Shader:
        case Kind::SharedShader: detail = "Shader"; break;
        case Kind::Texture:
        case Kind::SharedTexture: detail = "Texture"; break;
        case Kind::Buffer: detail = "Buffer"; break;
        case Kind::Geometry: detail = "Geometry"; break;
        case Kind::Input: detail = "OSC input"; break;
        case Kind::Preview: detail = "Enlarged preview"; break;
    }
    return prefix.empty() ? detail : prefix + " / " + detail;
}

bool InspectorView::isLayerShader() const {
    return pages.size() == 2 && pages.front().item.key.kind == Kind::Layer
        && pages.back().item.key.kind == Kind::Shader;
}

bool InspectorView::expanded() const {
    return !pages.empty() && pages.back().item.key.kind == Kind::Preview;
}

int InspectorView::height(bool compact) const {
    if (expanded()) return compact ? 350 : 434;
    if (pages.empty() && !globals) return compact ? 185 : 240;
    if (pages.size() > 1 && (pages.back().item.key.kind == Kind::Texture || pages.back().item.key.kind == Kind::Buffer))
        return compact ? 285 : 390;
    return compact ? 300 : 468;
}

} // namespace vosc::operator_ui
#endif
