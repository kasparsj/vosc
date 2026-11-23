#include "InspectorView.hpp"

#if VOSC_OPERATOR_UI
#include "../VOSC.hpp"
#include "../ResourceRegistry.hpp"
#include "../tex/ShaderTex.h"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/StringUtilities.h>
#include <array>
#include <sstream>

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

std::string control(const std::string& id, const std::string& text, bool selected = false,
                    uint64_t target = 0) {
    return "<button id='" + id + "'" + (selected ? " class='selected'" : "")
        + " data-target='" + std::to_string(target) + "'>" + encode(text) + "</button>";
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

std::string textureSourceLabel(const Texture& texture) {
    return texture.tex && !texture.tex->path.empty()
        ? ofFilePath::getFileName(texture.tex->path) : "Source unavailable";
}

std::string glName(unsigned type) {
    switch (type) {
        case GL_FLOAT: return "float";
        case GL_FLOAT_VEC2: return "vec2";
        case GL_FLOAT_VEC3: return "vec3";
        case GL_FLOAT_VEC4: return "vec4";
        case GL_INT: return "int";
        case GL_INT_VEC2: return "ivec2";
        case GL_INT_VEC3: return "ivec3";
        case GL_INT_VEC4: return "ivec4";
        case GL_BOOL: return "bool";
        case GL_FLOAT_MAT2: return "mat2";
        case GL_FLOAT_MAT3: return "mat3";
        case GL_FLOAT_MAT4: return "mat4";
        case GL_SAMPLER_2D: return "sampler2D";
        case GL_SAMPLER_3D: return "sampler3D";
        case GL_SAMPLER_CUBE: return "samplerCube";
        case GL_SAMPLER_BUFFER: return "samplerBuffer";
        case GL_SAMPLER_2D_ARRAY: return "sampler2DArray";
        case GL_TEXTURE_2D: return "GL_TEXTURE_2D";
        case GL_TEXTURE_RECTANGLE: return "GL_TEXTURE_RECTANGLE";
        case GL_TEXTURE_2D_ARRAY: return "GL_TEXTURE_2D_ARRAY";
        case GL_TEXTURE_3D: return "GL_TEXTURE_3D";
        case GL_TEXTURE_CUBE_MAP: return "GL_TEXTURE_CUBE_MAP";
        case GL_TEXTURE_BUFFER: return "GL_TEXTURE_BUFFER";
        case GL_RGBA: return "GL_RGBA";
        case GL_RGB: return "GL_RGB";
        case GL_RGBA8: return "GL_RGBA8";
        case GL_RGB8: return "GL_RGB8";
        case GL_RGBA16F: return "GL_RGBA16F";
        case GL_RGBA32F: return "GL_RGBA32F";
        case GL_R8: return "GL_R8";
        case GL_R32F: return "GL_R32F";
        case GL_REPEAT: return "Repeat";
        case GL_MIRRORED_REPEAT: return "Mirrored repeat";
        case GL_CLAMP_TO_EDGE: return "Clamp to edge";
        case GL_CLAMP_TO_BORDER: return "Clamp to border";
        case GL_LINEAR: return "Linear";
        case GL_NEAREST: return "Nearest";
        case GL_LINEAR_MIPMAP_LINEAR: return "Linear mipmap linear";
        case GL_LINEAR_MIPMAP_NEAREST: return "Linear mipmap nearest";
        case GL_NEAREST_MIPMAP_LINEAR: return "Nearest mipmap linear";
        case GL_NEAREST_MIPMAP_NEAREST: return "Nearest mipmap nearest";
        default: {
            std::ostringstream value;
            value << "0x" << std::hex << type;
            return value.str();
        }
    }
}

template<class T>
std::string numericValues(const std::vector<T>& values) {
    if (values.size() == 1) return ofToString(values.front());
    std::string text = "[";
    for (const auto value : values) {
        if (text.size() > 1) text += ", ";
        text += ofToString(value);
    }
    return text + "]";
}

std::pair<std::string, std::string> variableValue(const BaseVar* var) {
    if (const auto* value = dynamic_cast<const Variable<float>*>(var))
        return {value->size() == 1 ? "float" : "float array", numericValues(value->getVec())};
    if (const auto* value = dynamic_cast<const Variable<int>*>(var))
        return {value->size() == 1 ? "int" : "int array", numericValues(value->getVec())};
    if (var && var->size()) {
        if (const auto* value = dynamic_cast<const Variable<glm::vec2>*>(var))
            return {"vec2", ofToString(value->get())};
        if (const auto* value = dynamic_cast<const Variable<glm::vec3>*>(var))
            return {"vec3", ofToString(value->get())};
        if (const auto* value = dynamic_cast<const Variable<glm::vec4>*>(var))
            return {"vec4", ofToString(value->get())};
        if (const auto* value = dynamic_cast<const Variable<ofFloatColor>*>(var))
            return {"color", ofToString(value->get())};
    }
    return {"Unavailable", "Unavailable"};
}

std::string facts(const ofTexture& texture, bool buffer) {
    const auto& data = texture.getTextureData();
    std::string result = quiet("GL format · " + glName(data.glInternalFormat))
        + quiet("GL target · " + glName(data.textureTarget));
    if (buffer) return result + quiet("Wrap S / T · Unavailable") + quiet("Min / mag · Unavailable");
    return result + quiet("Wrap S / T · " + glName(data.wrapModeHorizontal) + " / " + glName(data.wrapModeVertical))
        + quiet("Min / mag · " + glName(data.minFilter) + " / " + glName(data.magFilter));
}

struct TextureAction {
    const char* id;
    const char* label;
    bool Texture::*flag;
    bool value;
};
constexpr std::array textureActions{
    TextureAction{"inspect-static-set", "Set static", &Texture::isStatic, true},
    TextureAction{"inspect-static-clear", "Clear static", &Texture::isStatic, false},
    TextureAction{"inspect-update-request", "Request update", &Texture::needsUpdate, true},
    TextureAction{"inspect-update-cancel", "Cancel pending update", &Texture::needsUpdate, false},
};

bool sameOwner(const std::weak_ptr<const void>& a, const std::weak_ptr<const void>& b) {
    return !a.owner_before(b) && !b.owner_before(a);
}

} // namespace

bool InspectorView::Identity::operator==(const Identity& other) const {
    return sameOwner(owner, other.owner) && sameOwner(backend, other.backend) &&
        program == other.program && sourceRevision == other.sourceRevision && source == other.source;
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
        result.identity.source = result.shader->operatorSourcePath();
    }
    if (result.texture) {
        result.identity.owner = result.texture;
        result.identity.backend = result.texture->tex;
        result.identity.source = textureSource(*result.texture);
        result.identity.sourceRevision = result.texture->operatorSourceRevision();
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
    nextReadings.clear();
    previewSlots.clear();
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

std::string InspectorView::rowId(const Key& key, bool rail) {
    return std::string(rail ? "inspect-rail-" : "inspect-row-")
        + std::to_string(static_cast<int>(key.kind)) + "-" + std::to_string(key.layer) + "-" + idPart(key.name);
}

std::string InspectorView::row(const Key& key, const Resource& current, const std::string& text,
                              const std::string& subtitle, const std::string& kind, bool rail) {
    const auto id = rowId(key, rail);
    const auto previous = visibleRows.find(id);
    uint64_t token;
    if (restoreRow && restoreRow->first == id && restoreRow->second.item.identity == current.identity)
        token = restoreRow->second.token;
    else if (previous != visibleRows.end() && previous->second.item.identity == current.identity)
        token = previous->second.token;
    else token = nextToken++;
    nextRows.insert_or_assign(id, Row{{key, current.identity}, token});
    if (key.kind == Kind::Preview) return control(id, text, false, token);
    return "<button class='inspect-row" + std::string(rail ? " inspect-rail-row" : "") + "' id='" + id
        + "' data-target='" + std::to_string(token) + "'>" + preview(current, true)
        + "<span class='inspect-row-label'><span>" + encode(text) + "</span>" + (subtitle.empty() ? "" : "<span class='inspect-subtitle'>" + encode(subtitle) + "</span>")
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
    if (resolved.resource.shader &&
        (id == "inspect-bindings" || id == "inspect-uniforms" || id == "inspect-variables")) {
        pages.back().section = id.substr(8);
        return Navigation{id};
    }
    if (!pages.empty() && (resolved.resource.texture || resolved.resource.buffer)) {
        auto& page = pages.back();
        if (id == "inspect-sampling" || id == "inspect-passes" || id == "inspect-texture-vars") {
            auto* open = id == "inspect-sampling" ? &page.sampling
                : id == "inspect-passes" ? &page.passes : &page.variables;
            *open = !*open;
            return Navigation{id, scroll};
        }
        if (expanded() && id.compare(0, 14, "inspect-slice-") == 0 && resolved.resource.texture) {
            const auto* output = inspectorTextureOutput(*resolved.resource.texture);
            const int depth = output ? inspectorArrayDepth(*output) : 0;
            if (depth <= 1) return {};
            if (id == "inspect-slice-prev") page.slice = (page.slice + depth - 1) % depth;
            else if (id == "inspect-slice-next") page.slice = (page.slice + 1) % depth;
            else if (id == "inspect-slice-select") page.choosingSlice = !page.choosingSlice;
            else if (token < static_cast<uint64_t>(depth)) {
                page.slice = static_cast<int>(token);
                page.choosingSlice = false;
                return Navigation{"inspect-slice-select", scroll};
            }
            return Navigation{id, scroll};
        }
        if (resolved.resource.texture && !expanded()) {
            for (const auto& action : textureActions) {
                if (id != action.id) continue;
                if (token != page.origin.token) return {};
                // Use the same route resolver here and after any trial restoration.
                resolved.resource.texture.reset();
                renderer.applyOperatorTextureFlag([&]() -> std::shared_ptr<Texture> {
                    auto live = resolvePages(renderer);
                    if (live.state != State::Live || !live.resource.texture
                        || !live.resource.texture->isLoaded()) return nullptr;
                    return live.resource.texture;
                }, action.flag, action.value);
                return {};
            }
        }
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
        if (item.key.kind == Kind::Layer) focus = rowId({Kind::Shader, {}});
        if (resource.shader) {
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
        if (texture.texture) out += row(key, texture, "Default texture", textureSourceLabel(*texture.texture), "Texture");
        else out += quiet("No default texture");
    }
    size_t named = textures.size() - (fromLayer && textures.count(DEFAULT_TEX) ? 1 : 0);
    out += heading("Texture bindings · " + std::to_string(named));
    for (const auto& [name, texture] : textures) {
        if (!texture || (fromLayer && name == DEFAULT_TEX)) continue;
        const Key key{Kind::Texture, name};
        out += row(key, resolve(renderer, key, resource), name, textureSourceLabel(*texture), "Texture");
    }
    out += heading("Buffers · " + std::to_string(buffers.size()));
    for (const auto& [name, buffer] : buffers) {
        if (!buffer) continue;
        const Key key{Kind::Buffer, name};
        out += row(key, resolve(renderer, key, resource), name, {}, "Buffer");
    }
    return out;
}

std::string InspectorView::preview(const Resource& resource, bool thumbnail, int slice) {
    if (!resource.texture && !resource.buffer && !resource.geometry && !resource.input) return {};
    if (resource.texture && !resource.texture->isLoaded()) return {};
    if (resource.geometry && !resource.geometry->hasMesh()) return {};
    const auto index = previewSlots.size();
    previewSlots.push_back({resource.texture, resource.buffer, resource.geometry, resource.input, slice});
    const auto slotClass = thumbnail ? "inspect-preview inspect-thumb" : "inspect-preview inspect-figure";
    if (thumbnail)
        return "<span class='" + std::string(slotClass) + "' data-preview='" + std::to_string(index) + "'></span>";
    return "<div class='" + std::string(slotClass) + "' data-preview='" + std::to_string(index) + "'></div>";
}

std::string InspectorView::reading(std::string value) {
    const auto index = nextReadings.size();
    nextReadings.push_back(std::move(value));
    return "<span class='inspect-reading' data-reading='" + std::to_string(index) + "'></span>";
}

void InspectorView::syncReadings(Rml::Element& content, bool layoutChanged) {
    if (!layoutChanged && nextReadings == renderedReadings) return;
    Rml::ElementList nodes;
    content.GetElementsByClassName(nodes, "inspect-reading");
    for (auto* node : nodes) {
        const auto index = node->GetAttribute<unsigned>("data-reading", 0);
        if (index >= nextReadings.size()) continue;
        if (layoutChanged || index >= renderedReadings.size() || nextReadings[index] != renderedReadings[index])
            node->SetInnerRML(encode(nextReadings[index]));
    }
    renderedReadings.swap(nextReadings);
}

std::string InspectorView::valueRow(const std::string& name, const std::string& type, std::string value) {
    return "<div class='inspect-value-row'><span class='inspect-value-name'>" + encode(name)
        + "</span><span class='inspect-value-type'>" + encode(type)
        + "</span>" + reading(std::move(value)) + "</div>";
}

std::string InspectorView::variableRows(const std::map<std::string, std::shared_ptr<BaseVar>>& variables) {
    if (variables.empty()) return quiet("No configured variables");
    std::string out;
    for (const auto& [name, variable] : variables) {
        auto [type, value] = variableValue(variable.get());
        out += valueRow(name, type, std::move(value));
    }
    return out;
}

std::string InspectorView::globalsMarkup(const VOSC& renderer) {
    std::string out = heading("Geometry");
    size_t shown = 0;
    for (const auto& layer : renderer.operatorLayers()) {
        if (!layer || !layer->geom || !layer->geom->isLoaded()) continue;
        const Key key{Kind::Geometry, {}, layer->index};
        out += row(key, resolve(renderer, key, {}), "Layer " + std::to_string(layer->index),
                   layer->geom->operatorSource(), "Wireframe");
        ++shown;
    }
    if (!shown) out += quiet("No geometry");
    const auto* registry = ResourceRegistry::current();
    out += heading("Shared textures");
    if (registry && !registry->textureSharedPool.empty()) {
        for (const auto& [name, texture] : registry->textureSharedPool) {
            if (!texture) continue;
            const Key key{Kind::SharedTexture, name};
            out += row(key, resolve(renderer, key, {}), name, textureSourceLabel(*texture), "Texture");
        }
    } else out += quiet("No shared textures");
    out += heading("Pooled shaders");
    if (registry && !registry->shaderSharedPool.empty()) {
        for (const auto& [name, shader] : registry->shaderSharedPool) {
            if (!shader) continue;
            const Key key{Kind::SharedShader, name};
            out += row(key, resolve(renderer, key, {}), name, sourceName(*shader), "Shader");
        }
    } else out += quiet("No pooled shaders");
    out += heading("OSC inputs");
    const auto& inputs = Inputs::get().all();
    for (const auto& [name, input] : inputs) {
        if (!input) continue;
        const Key key{Kind::Input, name};
        out += row(key, resolve(renderer, key, {}), name, {}, "History");
    }
    if (inputs.empty()) out += quiet("No configured OSC inputs");
    return out;
}

std::string InspectorView::shaderMarkup(const VOSC& renderer, const Resource& resource,
                                        const std::string& diagnostic) {
    const auto& page = pages.back();
    const auto& shader = *resource.shader;
    std::string out = label(page.item.key.name.empty() ? sourceName(shader) : page.item.key.name)
        + quiet(shaderDescription(shader));
    if (!shader.operatorSourcePath().empty()) out += quiet(shader.operatorSourcePath());
    out += diagnostic;
    if (page.section == "bindings") return out + bindings(renderer, resource, false);
    if (page.section == "variables")
        return out + heading("Configured VOSC variables · " + std::to_string(shader.vars.size())) + variableRows(shader.vars);
    if (!shader.isShaderLoaded()) return out + quiet("Linked uniforms unavailable");
    const auto& uniforms = shader.getUniforms();
    const auto values = shader.getUniformValues();
    out += heading("Linked uniforms · " + std::to_string(uniforms.size()));
    if (uniforms.empty()) return out + quiet("No reflected uniforms");
    for (const auto& [name, info] : uniforms) {
        const auto value = values.find(name);
        auto type = glName(info.type);
        if (info.isArray()) type += "[" + std::to_string(info.size) + "]";
        out += valueRow(name, type, value == values.end() ? "Unavailable" : value->second);
    }
    return out;
}

std::string InspectorView::textureMarkup(const VOSC& renderer, const Resource& resource) {
    auto& page = pages.back();
    const bool large = expanded();
    const size_t resourcePage = pages.size() - (large ? 2 : 1);
    const auto& key = pages[resourcePage].item.key;
    const bool defaultTexture = key.kind == Kind::Texture && key.name == DEFAULT_TEX && resourcePage > 0
        && pages[resourcePage - 1].item.key.kind == Kind::Layer;
    std::string out = label(defaultTexture ? "Default texture" : key.name);
    const bool buffer = static_cast<bool>(resource.buffer);
    const auto* output = buffer ? &resource.buffer->getTexture() : inspectorTextureOutput(*resource.texture);
    if (output && !output->isAllocated()) output = nullptr;
    bool shared = false;
    if (buffer) out += quiet("Buffer binding");
    else {
        const auto* registry = ResourceRegistry::current();
        if (registry) {
            for (const auto& [name, owner] : registry->textureSharedPool) {
                if (owner != resource.texture) continue;
                shared = true;
                out += quiet("Shared texture · " + name);
                break;
            }
        }
        out += quiet("Source · " + textureSource(*resource.texture));
    }
    if (output) {
        const auto depth = inspectorArrayDepth(*output);
        page.slice = depth > 0 ? std::clamp(page.slice, 0, depth - 1) : 0;
        out += preview(resource, false, page.slice);
        if (buffer) {
            out += quiet(std::to_string(inspectorBufferTexels(*resource.buffer)) + " texels · "
                + std::to_string(resource.buffer->buf.size()) + " bytes · RGB preview");
        } else {
            out += quiet(std::to_string(static_cast<int>(output->getWidth())) + " × "
                + std::to_string(static_cast<int>(output->getHeight()))
                + (depth > 0 ? " × " + std::to_string(depth) : "") + " · Texture output");
        }
        if (large) {
            if (depth > 1) {
                out += control("inspect-slice-prev", "Previous slice")
                    + control("inspect-slice-select", "Slice " + std::to_string(page.slice + 1)
                              + " of " + std::to_string(depth), page.choosingSlice)
                    + control("inspect-slice-next", "Next slice");
                if (page.choosingSlice) {
                    for (int i = 0; i < depth; ++i)
                        out += control("inspect-slice-to-" + std::to_string(i), "Slice " + std::to_string(i + 1),
                                       i == page.slice, i);
                }
            }
            return out;
        }
        out += row({Kind::Preview, {}}, resource, "Enlarge preview");
    } else out += quiet(buffer ? "Buffer preview unavailable" : "Texture output unavailable");
    if (large) return out;
    if (!buffer && resource.texture->hasShader()) {
        const Key shaderKey{Kind::Shader, {}};
        const auto source = resolve(renderer, shaderKey, resource);
        if (source.shader) out += row(shaderKey, source, "Inspect source shader", sourceName(*source.shader), "Shader");
    }
    out += "<div class='inspect-disclosure'>" + control("inspect-sampling", "Sampling details", page.sampling);
    if (page.sampling) out += output ? facts(*output, buffer) : quiet("Sampling details unavailable");
    out += "</div>";
    if (buffer) return out;
    const auto& texture = *resource.texture;
    out += "<div class='inspect-disclosure'>"
        + control("inspect-passes", "Shader passes · " + std::to_string(texture.passes.size()), page.passes);
    if (page.passes) {
        if (texture.passes.empty()) out += quiet("No shader passes");
        for (size_t i = 0; i < texture.passes.size(); ++i) {
            const auto& pass = texture.passes[i];
            const auto shader = pass ? pass->getShader() : nullptr;
            out += quiet(std::to_string(i + 1) + ". " + (shader ? sourceName(*shader) : "Unavailable"));
        }
    }
    out += "</div><div class='inspect-disclosure'>"
        + control("inspect-texture-vars", "Texture variables · " + std::to_string(texture.vars.size()), page.variables);
    if (page.variables) out += variableRows(texture.vars);
    out += "</div>";
    if (texture.isLoaded()) {
        out += heading("Texture updates");
        if (shared) out += quiet("Updates affect every binding of this shared texture");
        out += "<div class='caption'>" + reading(texture.isStatic ? "Static · On" : "Static · Off") + " · "
            + reading(texture.needsUpdate ? "Update · Pending" : "Update · Idle") + "</div>";
        for (const auto& action : textureActions) out += control(action.id, action.label, false, page.origin.token);
    }
    return out;
}

std::string InspectorView::auxiliaryMarkup(const Resource& resource) {
    if (resource.geometry) {
        const auto& geometry = *resource.geometry;
        return label("Geometry wireframe") + quiet(geometry.operatorSource())
            + (geometry.hasMesh() ? preview(resource, false) : quiet("Geometry preview unavailable"))
            + (geometry.drawInstanced ? quiet(std::to_string(geometry.drawInstanced) + " instances") : "");
    }
    if (resource.input) {
        return label(pages.back().item.key.name) + preview(resource, false)
            + "<div class='caption'>Amplitude · " + reading(ofToString(resource.input->ampScaled, 2)) + "</div>";
    }
    return {};
}

std::string InspectorView::markup(const VOSC& renderer, const std::string& shaderDiagnostic) {
    const auto& layers = renderer.operatorLayers();
    if (pages.empty()) {
        if (globals) return globalsMarkup(renderer);
        std::string out = heading("OSC layers · " + std::to_string(layers.size()));
        for (const auto& layer : layers) if (layer) out += layerLink(*layer, renderer);
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
        const Key geometryKey{Kind::Geometry, {}, resource.layer->index};
        const auto geometry = resolve(renderer, geometryKey, resource);
        const Key shaderKey{Kind::Shader, {}};
        return "<div class='inspect-layer-columns'>" + rail + "</div><div class='inspect-layer-body'>"
            + label("Layer " + std::to_string(resource.layer->index)) + heading("Geometry")
            + (geometry.geometry ? row(geometryKey, geometry, geometry.geometry->operatorSource(), {}, "Wireframe")
                                 : quiet("No geometry"))
            + heading("Shader")
            + row(shaderKey, resolve(renderer, shaderKey, resource), sourceName(resource.layer->shader),
                  shaderDescription(resource.layer->shader), resource.layer->reloadDiagnostic() ? "Reload failed" : "Shader")
            + bindings(renderer, resource, true) + "</div></div>";
    }
    if (resource.shader) return shaderMarkup(renderer, resource, shaderDiagnostic);
    if (resource.texture || resource.buffer) return textureMarkup(renderer, resource);
    return auxiliaryMarkup(resource);
}

std::shared_ptr<Layer> InspectorView::selectedLayer(const VOSC& renderer) const {
    if (pages.empty() || pages.front().item.key.kind != Kind::Layer) return {};
    auto layer = resolve(renderer, pages.front().item.key, {});
    return layer.identity == pages.front().item.identity ? layer.layer : nullptr;
}

std::string InspectorView::title() const {
    if (pages.empty()) return "Inspector";
    std::string prefix;
    const auto& root = pages.front().item.key;
    if (root.kind == Kind::Layer || root.kind == Kind::Geometry)
        prefix = "Layer " + std::to_string(root.layer);
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

std::string InspectorView::toolbar() const {
    if (pages.empty())
        return control("inspect-layers", "Layers", !globals) + control("inspect-globals", "Globals", globals);
    if (invalid != State::Live) return {};
    const auto& page = pages.back();
    if (page.item.key.kind != Kind::Shader && page.item.key.kind != Kind::SharedShader) return {};
    return control("inspect-bindings", "Bindings", page.section == "bindings")
        + control("inspect-uniforms", "Uniforms", page.section == "uniforms")
        + control("inspect-variables", "Variables", page.section == "variables");
}

int InspectorView::height(const VOSC& renderer, bool compact) const {
    if (expanded()) return compact ? 350 : 434;
    if (pages.empty() && !globals) {
        const auto count = std::min<size_t>(renderer.operatorLayers().size(), 8);
        return std::min(compact ? 290 : 445, 142 + static_cast<int>(count) * 44);
    }
    if (pages.size() > 1 && (pages.back().item.key.kind == Kind::Texture || pages.back().item.key.kind == Kind::Buffer))
        return compact ? 285 : 390;
    return compact ? 300 : 468;
}

} // namespace vosc::operator_ui
#endif
