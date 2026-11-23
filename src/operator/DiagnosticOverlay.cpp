#include "DiagnosticOverlay.hpp"

#if VOSC_OPERATOR_UI
#include "DiagnosticMarkup.hpp"
#include "BrowseView.hpp"
#include "InspectorView.hpp"
#include "LocalizedDockBlur.hpp"

#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <RmlUi/Core/Elements/ElementFormControlSelect.h>
#include <RmlUi/Core/StringUtilities.h>
#include <RmlUi_Renderer_GL3.h>
#include <VoscOperatorFont.hpp>
#include <ofAppGLFWWindow.h>
#include <ofImage.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace {
constexpr int cleanOutputPress = -1;

const char* severity(ofLogLevel level) {
    switch (level) {
        case OF_LOG_FATAL_ERROR: return "Fatal";
        case OF_LOG_ERROR: return "Error";
        case OF_LOG_WARNING: return "Warning";
        case OF_LOG_NOTICE: return "Notice";
        default: return "Verbose";
    }
}

constexpr bool isError(ofLogLevel level) {
    return level == OF_LOG_ERROR || level == OF_LOG_FATAL_ERROR;
}
static_assert(!isError(OF_LOG_WARNING) && isError(OF_LOG_ERROR)
    && isError(OF_LOG_FATAL_ERROR) && !isError(OF_LOG_SILENT));

std::string foldAscii(std::string text) {
    for (auto& c : text) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    return text;
}

bool matchesDiagnostic(const LoggerLine& entry, const std::string& foldedQuery, bool errorsOnly) {
    if (errorsOnly && !isError(entry.level)) return false;
    if (foldedQuery.empty()) return true;
    return foldAscii(entry.message).find(foldedQuery) != std::string::npos
        || foldAscii(entry.module).find(foldedQuery) != std::string::npos
        || foldAscii(severity(entry.level)).find(foldedQuery) != std::string::npos
        || ("#" + std::to_string(entry.sequence)).find(foldedQuery) != std::string::npos;
}

void appendPasteboardText(std::string& output, std::string_view text, bool& escaped) {
    for (const char c : text) {
        if (c == '\0') {
            output += "\\0";
            escaped = true;
        } else {
            output += c;
        }
    }
}

std::string elapsedLabel(float seconds) {
    const auto milliseconds = static_cast<uint64_t>(std::max(0.f, seconds) * 1000.f);
    char result[32];
    std::snprintf(result, sizeof(result), "%02llu:%02u.%03u",
        static_cast<unsigned long long>(milliseconds / 60000),
        static_cast<unsigned>((milliseconds / 1000) % 60), static_cast<unsigned>(milliseconds % 1000));
    return result;
}

class OperatorClipboard final : public Rml::SystemInterface {
public:
    void SetClipboardText(const Rml::String& text) override { ofSetClipboardString(text); }
    void GetClipboardText(Rml::String& text) override { text = ofGetClipboardString(); }

    bool copyBrowseCommand(const Rml::String& text) {
#if defined(VOSC_BROWSE_CLIPBOARD_TEST) && VOSC_BROWSE_CLIPBOARD_TEST
        if (!std::exchange(testedFailure, true)) return false;
#endif
        glfwGetError(nullptr);
        SetClipboardText(text);
        if (glfwGetError(nullptr) != GLFW_NO_ERROR) return false;
        Rml::String actual;
        GetClipboardText(actual);
        return glfwGetError(nullptr) == GLFW_NO_ERROR && actual == text;
    }

private:
#if defined(VOSC_BROWSE_CLIPBOARD_TEST) && VOSC_BROWSE_CLIPBOARD_TEST
    bool testedFailure = false;
#endif
};

int modifiers(int ofModifiers) {
    return ((ofModifiers & OF_KEY_SHIFT) ? Rml::Input::KM_SHIFT : 0)
         | ((ofModifiers & OF_KEY_CONTROL) ? Rml::Input::KM_CTRL : 0)
         | ((ofModifiers & OF_KEY_ALT) ? Rml::Input::KM_ALT : 0)
         | ((ofModifiers & OF_KEY_SUPER) ? Rml::Input::KM_META : 0);
}

Rml::Input::KeyIdentifier editingKey(int glfwKey, int& flags) {
    using namespace Rml::Input;
#ifdef TARGET_OSX
    if (flags & KM_META) {
        if (glfwKey == GLFW_KEY_LEFT) return KI_HOME;
        if (glfwKey == GLFW_KEY_RIGHT) return KI_END;
        flags |= KM_CTRL;
    } else if ((flags & KM_ALT) && (glfwKey == GLFW_KEY_LEFT || glfwKey == GLFW_KEY_RIGHT
        || glfwKey == GLFW_KEY_BACKSPACE || glfwKey == GLFW_KEY_DELETE)) {
        flags = (flags & ~KM_ALT) | KM_CTRL;
    }
#endif
    switch (glfwKey) {
        case GLFW_KEY_LEFT: return KI_LEFT;
        case GLFW_KEY_RIGHT: return KI_RIGHT;
        case GLFW_KEY_UP: return KI_UP;
        case GLFW_KEY_DOWN: return KI_DOWN;
        case GLFW_KEY_HOME: return KI_HOME;
        case GLFW_KEY_END: return KI_END;
        case GLFW_KEY_BACKSPACE: return KI_BACK;
        case GLFW_KEY_DELETE: return KI_DELETE;
        case GLFW_KEY_A: return KI_A;
        case GLFW_KEY_C: return KI_C;
        case GLFW_KEY_X: return KI_X;
        case GLFW_KEY_V: return KI_V;
        default: return KI_UNKNOWN;
    }
}

bool repeatsWhenEditing(Rml::Input::KeyIdentifier key) {
    using namespace Rml::Input;
    return key == KI_LEFT || key == KI_RIGHT || key == KI_UP || key == KI_DOWN
        || key == KI_HOME || key == KI_END || key == KI_BACK || key == KI_DELETE;
}

std::string message(const std::string& text, bool error = false) {
    return std::string(error ? "<div class='message error'>" : "<div class='message'>")
        + Rml::StringUtilities::EncodeRml(text) + "</div>";
}

std::string button(const std::string& id, const std::string& label, uint64_t target = 0) {
    return "<button id='" + id + "' data-target='" + std::to_string(target) + "'>"
        + Rml::StringUtilities::EncodeRml(label) + "</button>";
}

// The reference backend restores raster state but does not restore these bindings.
class ForegroundState {
public:
    ForegroundState() {
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &readFramebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFramebuffer);
        glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &arrayBuffer);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &program);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glGetBooleanv(GL_COLOR_WRITEMASK, colorMask.data());
        srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
        glGetIntegerv(GL_SAMPLER_BINDING, &sampler);

        glBindSampler(0, 0);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    ~ForegroundState() {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, drawFramebuffer);
        glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
        glUseProgram(program);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, arrayBuffer);
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, unpackBuffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindSampler(0, sampler);
        glActiveTexture(activeTexture);
        glColorMask(colorMask[0], colorMask[1], colorMask[2], colorMask[3]);
        if (srgb) glEnable(GL_FRAMEBUFFER_SRGB);
    }

private:
    GLint readFramebuffer = 0, drawFramebuffer = 0, renderbuffer = 0;
    GLint vao = 0, arrayBuffer = 0, unpackBuffer = 0, program = 0;
    GLint activeTexture = GL_TEXTURE0, texture = 0, sampler = 0;
    GLboolean srgb = GL_FALSE;
    std::array<GLboolean, 4> colorMask{};
};
class OperatorRenderInterface final : public RenderInterface_GL3 {
    Rml::TextureHandle LoadTexture(Rml::Vector2i& size, const Rml::String& source) override {
        constexpr std::string_view browsePrefix = "browse:";
        if (source.compare(0, browsePrefix.size(), browsePrefix) != 0)
            return RenderInterface_GL3::LoadTexture(size, source);
        const auto path = vosc::operator_ui::stagedBrowseFile(source.substr(browsePrefix.size()));
        ofPixels pixels;
        if (!path || !ofLoadImage(pixels, *path)) return {};
        const auto longest = std::max(pixels.getWidth(), pixels.getHeight());
        if (longest > 480) {
            pixels.resize(std::max<size_t>(1, pixels.getWidth() * 480 / longest),
                          std::max<size_t>(1, pixels.getHeight() * 480 / longest));
        }
        pixels.setImageType(OF_IMAGE_COLOR_ALPHA);
        for (auto pixel : pixels.getPixelsIter()) {
            const auto alpha = pixel[3];
            for (size_t channel = 0; channel < 3; ++channel)
                pixel[channel] = static_cast<uint8_t>((pixel[channel] * alpha + 127) / 255);
        }
        size = {static_cast<int>(pixels.getWidth()), static_cast<int>(pixels.getHeight())};
        // RmlUi can upload an image while resolving its intrinsic size in Update.
        const ForegroundState state;
        constexpr std::array<GLenum, 4> unpackStorage{
            GL_UNPACK_ALIGNMENT, GL_UNPACK_ROW_LENGTH, GL_UNPACK_SKIP_ROWS, GL_UNPACK_SKIP_PIXELS,
        };
        std::array<GLint, unpackStorage.size()> previous{};
        for (size_t i = 0; i < unpackStorage.size(); ++i) {
            glGetIntegerv(unpackStorage[i], &previous[i]);
            glPixelStorei(unpackStorage[i], i == 0 ? 1 : 0);
        }
        const auto texture = GenerateTexture({pixels.getData(), pixels.size()}, size);
        for (size_t i = 0; i < unpackStorage.size(); ++i)
            glPixelStorei(unpackStorage[i], previous[i]);
        return texture;
    }
};

}

struct DiagnosticOverlay::Surface final : Rml::EventListener {
    struct Destination {
        View view;
        const char* id;
        const char* title;
    };
    static constexpr std::array destinations{
        Destination{View::Overview, "overview", "Overview"},
        Destination{View::Inspector, "inspector", "Inspector"},
        Destination{View::Console, "console", "Console"},
        Destination{View::Browse, "browse", "Browse"},
    };

    struct Action {
        std::string id;
        uint64_t target = 0;
    };

    ~Surface() override {
        if (initialized) Rml::Shutdown();
        renderer.reset();
        if (loaderInitialized) RmlGL3::Shutdown();
        blur.shutdown();
    }

    void setup() {
        const ForegroundState state;
        Rml::String detail;
        if (!RmlGL3::Initialize(&detail)) throw std::runtime_error("RmlUi GL3 loader: " + detail);
        loaderInitialized = true;
        renderer = std::make_unique<OperatorRenderInterface>();
        if (!*renderer) throw std::runtime_error("RmlUi GL3 renderer could not initialize");
        Rml::SetSystemInterface(&clipboard);
        Rml::SetRenderInterface(renderer.get());
        if (!Rml::Initialise()) throw std::runtime_error("RmlUi could not initialize");
        initialized = true;
        const auto style = Rml::Style::FontStyle::Normal;
        if (!Rml::LoadFontFace({voscOperatorFontRegular, sizeof(voscOperatorFontRegular)},
                               "LatoLatin", style, Rml::Style::FontWeight::Normal)
            || !Rml::LoadFontFace({voscOperatorFontBold, sizeof(voscOperatorFontBold)},
                                  "LatoLatin", style, Rml::Style::FontWeight::Bold)) {
            throw std::runtime_error("RmlUi could not load the operator fonts");
        }
        context = Rml::CreateContext("vosc-operator", {1, 1});
        if (!context) throw std::runtime_error("RmlUi could not create the operator context");
        context->EnableMouseCursor(false);
        context->SetDefaultScrollBehavior(Rml::ScrollBehavior::Instant, 1.f);
        document = context->LoadDocumentFromMemory(vosc::operator_ui::documentMarkup);
        if (!document) throw std::runtime_error("RmlUi could not load the operator controls");
        document->AddEventListener(Rml::EventId::Click, this);
        document->AddEventListener(Rml::EventId::Change, this);
        dock = document->GetElementById("dock");
        panel = document->GetElementById("panel");
        content = document->GetElementById("content");
        rows = document->GetElementById("log-rows");
        auto* input = document->GetElementById("console-search");
        if (!input || input->GetTagName() != "input")
            throw std::runtime_error("RmlUi could not load the Console search input");
        search = static_cast<Rml::ElementFormControlInput*>(input);
        browseSearch = static_cast<Rml::ElementFormControlInput*>(document->GetElementById("browse-search"));
        browseCategory = static_cast<Rml::ElementFormControlSelect*>(document->GetElementById("browse-category"));
    }

    void ProcessEvent(Rml::Event& event) override {
        const auto* target = event.GetTargetElement();
        if (event.GetId() == Rml::EventId::Change) {
            if (syncingBrowse || (target->GetId() != "browse-use" && target->GetId() != "browse-target")) return;
            const auto value = event.GetParameter<Rml::String>("value", "");
            uint64_t token = 0;
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), token);
            if (parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size())
                browseChanges.push_back({target->GetId(), token});
            return;
        }
        if (target->GetTagName() != "button") target = target->Closest("button");
        if (!target || target->IsClassSet("inactive")) return;
        const auto value = target->GetAttribute<Rml::String>("data-target", "");
        uint64_t targetId = 0;
        std::from_chars(value.data(), value.data() + value.size(), targetId);
        action = Action{target->GetId(), targetId};
    }

    void syncViewport(const GLint* viewport) {
        auto* window = dynamic_cast<ofAppGLFWWindow*>(ofGetWindowPtr());
        const float scale = window ? std::max(1, window->getPixelScreenCoordScale()) : 1;
        const Rml::Vector2i size{viewport[2], viewport[3]};
        if (size != context->GetDimensions() || scale != context->GetDensityIndependentPixelRatio()) {
            const bool compact = size.x <= 720 * scale || size.y <= 540 * scale;
            const auto height = std::max(0, size.y - static_cast<int>((compact ? 69 : 102) * scale));
            panel->SetProperty("max-height", std::to_string(height) + "px");
            document->SetClass("compact", compact);
            document->SetClass("short", height < 185 * scale);
            dockRadius = (compact ? 21.5f : 25.0f) * scale;
        }
        if (size != context->GetDimensions()) context->SetDimensions(size);
        if (scale != context->GetDensityIndependentPixelRatio()) context->SetDensityIndependentPixelRatio(scale);
        renderer->SetViewport(size.x, size.y, viewport[0], viewport[1]);
    }

    void setReading(const char* id, std::string& cached, std::string next) {
        if (cached == next) return;
        document->GetElementById(id)->SetInnerRML(Rml::StringUtilities::EncodeRml(next));
        cached = std::move(next);
    }

    Rml::Element* row(uint64_t sequence) const {
        return sequence ? document->GetElementById("log-" + std::to_string(sequence)) : nullptr;
    }

    bool syncRows(uint64_t selectedSequence) {
        const bool changed = renderedSequences.size() != matches.size()
            || !std::equal(renderedSequences.begin(), renderedSequences.end(), matches.begin(),
                [](uint64_t sequence, const LoggerLine* entry) { return sequence == entry->sequence; });
        if (changed) {
            std::string markup;
            renderedSequences.clear();
            for (const auto* entry : matches) {
                renderedSequences.push_back(entry->sequence);
                const auto key = std::to_string(entry->sequence);
                std::string text;
                bool escaped = false;
                appendPasteboardText(text, entry->message, escaped);
                markup += "<button class='log-row' id='log-" + key + "' data-target='" + key
                    + "'><span class='log-clock'><span>" + elapsedLabel(entry->time)
                    + "</span><span>#" + key + "</span></span><span class='log-severity"
                    + (isError(entry->level) ? " error" : "") + "'>" + severity(entry->level)
                    + "</span><span class='log-text'><span class='log-message'>"
                    + Rml::StringUtilities::EncodeRml(text) + "</span>";
                if (!entry->module.empty())
                    markup += "<span class='log-module'>" + Rml::StringUtilities::EncodeRml(entry->module) + "</span>";
                markup += "</span></button>";
            }
            rows->SetInnerRML(markup);
        }
        if (changed || highlighted != selectedSequence) {
            if (highlighted && !changed) {
                if (auto* previous = row(highlighted)) previous->SetClass("selected", false);
            }
            if (auto* selected = row(selectedSequence)) selected->SetClass("selected", true);
            highlighted = selectedSequence;
        }
        return changed;
    }

    void observeLogScroll() {
        const auto top = rows->GetScrollTop();
        if (std::abs(top - observedScroll) > .5f)
            following = top + rows->GetClientHeight() >= rows->GetScrollHeight() - 1.f;
        observedScroll = top;
        if (!following) rememberReadingPosition();
    }

    void rememberReadingPosition() {
        const auto viewportTop = rows->GetAbsoluteTop() + rows->GetClientTop();
        anchorSequence = 0;
        for (int i = 0; i < rows->GetNumChildren(); ++i) {
            auto* element = rows->GetChild(i);
            const auto top = element->GetAbsoluteTop();
            if (top + element->GetOffsetHeight() <= viewportTop) continue;
            anchorSequence = element->GetAttribute<uint64_t>("data-target", 0);
            anchorOffset = viewportTop - top;
            break;
        }
    }

    void scrollToRow(Rml::Element* element, float offset) {
        const auto top = element->GetAbsoluteTop() - rows->GetAbsoluteTop() - rows->GetClientTop();
        rows->SetScrollTop(rows->GetScrollTop() + top + offset);
    }

    void placeLogScroll(bool rowsChanged) {
        const auto bottom = rows->GetScrollHeight() - rows->GetClientHeight();
        const bool geometryChanged = bottom != lastScrollLimit;
        lastScrollLimit = bottom;
        if (revealSequence) {
            if (auto* element = row(revealSequence)) scrollToRow(element, 0);
            revealSequence = 0;
        } else if (following && (rowsChanged || restoreScroll || geometryChanged || requestTail)) {
            rows->SetScrollTop(bottom);
        } else if (!following && (rowsChanged || restoreScroll || geometryChanged)) {
            if (auto* element = row(anchorSequence)) scrollToRow(element, anchorOffset);
            else rows->SetScrollTop(0);
        }
        requestTail = false;
        observedScroll = rows->GetScrollTop();
        if (!following) rememberReadingPosition();
    }

    void paint(const GLint* viewport) {
        const auto offset = dock->GetAbsoluteOffset(Rml::BoxArea::Border);
        const auto size = dock->GetBox().GetSize(Rml::BoxArea::Border);
        const float scale = context->GetDensityIndependentPixelRatio();
        if (!blur.draw(viewport, offset.x, offset.y, size.x, size.y, dockRadius, 19 * scale)
            && !reportedBlurError) {
            reportedBlurError = true;
            ofLogError("Operator UI") << "Could not draw the diagnostic dock backdrop";
        }
        const ForegroundState state;
        renderer->BeginFrame();
        context->Render();
        renderer->EndFrame();
    }

    std::unique_ptr<RenderInterface_GL3> renderer;
    OperatorClipboard clipboard;
    LocalizedDockBlur blur;
    Rml::Context* context = nullptr;
    Rml::ElementDocument* document = nullptr;
    Rml::Element* dock = nullptr;
    Rml::Element* panel = nullptr;
    Rml::Element* content = nullptr;
    Rml::Element* rows = nullptr;
    Rml::ElementFormControlInput* search = nullptr;
    Rml::ElementFormControlInput* browseSearch = nullptr;
    Rml::ElementFormControlSelect* browseCategory = nullptr;
    bool loaderInitialized = false;
    bool initialized = false;
    bool reportedBlurError = false;
    float dockRadius = 25.0f;
    std::optional<Action> action;
    std::string focusRequest;
    uint64_t focusOrigin = 0;
    vosc::operator_ui::BrowseView browse;
    std::vector<Action> browseChanges;
    std::string browseHeading, browseSummary;
    bool syncingBrowse = false;
    vosc::operator_ui::InspectorView inspector;
    vosc::operator_ui::InspectorPreview previews;
    uint64_t revealSequence = 0;
    uint64_t highlighted = 0, anchorSequence = 0;
    float observedScroll = 0, anchorOffset = 0, lastScrollLimit = 0;
    bool requestTail = false;
    bool following = true, errorsOnly = false, matchesDirty = true, filtersCleared = false;
    std::string query, copyFeedback, consoleStatus, followLabel = "Following latest";
    std::vector<const LoggerLine*> matches;
    std::vector<uint64_t> renderedSequences;
    std::map<int, Rml::Input::KeyIdentifier> forwardedKeys;
    std::string renderedContent, renderedTools, viewTitle, title, dimensions, fps, cameraPosition;
    bool restoreScroll = false;
    std::array<float, static_cast<size_t>(View::Count)> scroll{};
};

DiagnosticOverlay::DiagnosticOverlay() = default;
DiagnosticOverlay::~DiagnosticOverlay() = default;

void DiagnosticOverlay::setup() {
    auto ui = std::make_unique<Surface>();
    ui->setup();
    surface = std::move(ui);

    auto& events = ofEvents();
    constexpr int priority = OF_EVENT_ORDER_BEFORE_APP;
    listeners.push(events.keyPressed.newListener([this](ofKeyEventArgs& event) {
        if (!open && ownedKeys.count(event.keycode)) return true;
        return keyPressed(event);
    }, priority));
    listeners.push(events.keyReleased.newListener([this](ofKeyEventArgs& event) {
        const auto key = surface->forwardedKeys.extract(event.keycode);
        if (!key.empty()) surface->context->ProcessKeyUp(key.mapped(), modifiers(event.modifiers));
        return ownedKeys.erase(event.keycode) != 0;
    }, priority));
    listeners.push(events.charEvent.newListener([this, &events](uint32_t& codepoint) {
        if (!open) return false;
        const auto* focused = surface->context->GetFocusElement();
        const bool textFocus = (view == View::Console && focused == surface->search)
            || (view == View::Browse && focused == surface->browseSearch);
        if (textFocus && surface->focusRequest.empty()
            && !(events.getModifiers() & (OF_KEY_CONTROL | OF_KEY_SUPER))
            && codepoint >= 0x20 && codepoint != 0x7f)
            surface->context->ProcessTextInput(static_cast<Rml::Character>(codepoint));
        return true;
    }, priority));

    const auto pointer = [this](ofMouseEventArgs& event) { return pointerEvent(event); };
    listeners.push(events.mouseMoved.newListener(pointer, priority));
    listeners.push(events.mouseDragged.newListener(pointer, priority));
    listeners.push(events.mousePressed.newListener(pointer, priority));
    listeners.push(events.mouseReleased.newListener(pointer, priority));
    listeners.push(events.mouseScrolled.newListener(pointer, priority));
    listeners.push(events.mouseEntered.newListener(pointer, priority));
    listeners.push(events.mouseExited.newListener(pointer, priority));
}

void DiagnosticOverlay::shutdown() {
    listeners.unsubscribeAll();
    surface.reset();
    open = false;
}

void DiagnosticOverlay::rememberScene(const scene::File& scene, const VOSC& renderer) {
    sceneTitle = scene.title;
    sceneLayers.clear();
    for (const auto& layer : renderer.operatorLayers()) {
        if (layer) sceneLayers.push_back({layer, layer->shader.operatorProgram(),
            layer->shader.isShaderLoaded() ? layer->shader.getShader().getProgram() : 0, {}});
        else sceneLayers.emplace_back();
    }
    for (const auto& command : scene.commands) {
        if (command.asset.empty() || command.message.getArgAsString(2) != "/shader") continue;
        const auto layer = command.message.getArgAsInt32(3);
        if (layer >= 0 && static_cast<size_t>(layer) < sceneLayers.size())
            sceneLayers[layer].asset = command.asset.filename().string();
    }
}

void DiagnosticOverlay::show(View next, const std::string& focus) {
    surface->action.reset();
    surface->browseChanges.clear();
    cleanClick.reset();
    if (!open) {
        surface->document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None, Rml::ScrollFlag::None);
    } else if (!surface->restoreScroll) {
        if (view == View::Console) surface->observeLogScroll();
        else surface->scroll[static_cast<size_t>(view)] = surface->content->GetScrollTop();
    }
    open = true;
    view = next;
    surface->focusRequest = focus;
    surface->focusOrigin = 0;
    surface->restoreScroll = true;
}

void DiagnosticOverlay::close() {
    if (!surface->restoreScroll) {
        if (view == View::Console) surface->observeLogScroll();
        else surface->scroll[static_cast<size_t>(view)] = surface->content->GetScrollTop();
    }
    surface->context->ProcessMouseLeave();
    for (const auto& button : ownedButtons) {
        if (button.second != cleanOutputPress)
            surface->context->ProcessMouseButtonUp(button.second, 0);
    }
    surface->document->Hide();
    surface->action.reset();
    surface->browseChanges.clear();
    surface->focusRequest.clear();
    surface->revealSequence = 0;
    surface->inspector.reset();
    open = false;
}

void DiagnosticOverlay::showDiagnostic(uint64_t sequence) {
    const auto* entry = diagnostic(sequence);
    surface->inspector.reset();
    show(View::Console, entry ? "copy" : "console-search");
    selectedSequence = sequence;
    surface->revealSequence = sequence;
    surface->following = false;
    surface->copyFeedback.clear();
    surface->filtersCleared = entry
        && !matchesDiagnostic(*entry, foldAscii(surface->search->GetValue()), surface->errorsOnly);
    if (surface->filtersCleared) {
        surface->search->SetValue("");
        surface->query.clear();
        surface->errorsOnly = false;
        surface->matchesDirty = true;
    }
}

bool DiagnosticOverlay::keyPressed(ofKeyEventArgs& event) {
    auto& ui = *surface;
    auto* focused = ui.context->GetFocusElement();
    const bool textFocus = open && ui.focusRequest.empty()
        && ((view == View::Console && focused == ui.search) || (view == View::Browse && focused == ui.browseSearch));
    const bool browseSelect = open && view == View::Browse && focused && focused->GetTagName() == "select";
    const bool shortcut = !(event.modifiers & (OF_KEY_CONTROL | OF_KEY_ALT | OF_KEY_SUPER));
    const bool overview = shortcut && (event.key == 'o' || event.key == 'O');
    const bool console = shortcut && !textFocus && (event.key == 'c' || event.key == 'C');
    const bool inspector = event.modifiers == 0 && event.key == OF_KEY_TAB;
    if (!open && !overview && !console && !inspector) return false;

    const bool first = ownedKeys.insert(event.keycode).second && !event.isRepeat;
    if (first && !open) {
        if (overview) show(View::Overview, "overview");
        else if (inspector) show(View::Inspector, "inspector");
        else show(View::Console, "console");
        return true;
    }
    if (!open) return true;
    if (first && event.key == OF_KEY_ESC) {
        close();
        return true;
    }
    if (first && console && view != View::Console) {
        ui.inspector.reset();
        show(View::Console, "console");
        return true;
    }
    if (first && shortcut && (event.key == OF_KEY_LEFT || event.key == OF_KEY_RIGHT) && !textFocus) {
        const auto id = !ui.focusRequest.empty() ? ui.focusRequest : focused ? focused->GetId() : "";
        const auto& docks = Surface::destinations;
        const auto current = std::find_if(docks.begin(), docks.end(),
            [&](const auto& dest) { return id == dest.id; });
        if (current != docks.end()) {
            const auto step = event.key == OF_KEY_RIGHT ? 1 : docks.size() - 1;
            const auto& next = docks[(std::distance(docks.begin(), current) + step) % docks.size()];
            ui.inspector.reset();
            show(next.view, next.id);
        }
        return true;
    }
    if (!ui.focusRequest.empty()) return true;
    if (shortcut && focused && !textFocus && (event.key == OF_KEY_UP || event.key == OF_KEY_DOWN)) {
        if (browseSelect) {
            if (first) {
                const auto key = event.key == OF_KEY_UP ? Rml::Input::KI_UP : Rml::Input::KI_DOWN;
                ui.forwardedKeys.insert_or_assign(event.keycode, key);
                ui.context->ProcessKeyDown(key, 0);
            }
            return true;
        }
        const char* rowClass = focused->IsClassSet("log-row") ? "log-row"
            : focused->IsClassSet("inspect-row") ? "inspect-row" : nullptr;
        if (rowClass) {
            auto* next = focused;
            do {
                next = event.key == OF_KEY_UP ? next->GetPreviousSibling() : next->GetNextSibling();
            } while (next && !next->IsClassSet(rowClass));
            if (next) {
                next->Focus(true);
                next->ScrollIntoView(Rml::ScrollAlignment::Adaptive);
            }
            return true;
        }
        if (view == View::Inspector || view == View::Browse) {
            const float step = 32 * ui.context->GetDensityIndependentPixelRatio();
            ui.content->SetScrollTop(ui.content->GetScrollTop() + (event.key == OF_KEY_UP ? -step : step));
            return true;
        }
    }

    using namespace Rml::Input;
    auto flags = modifiers(event.modifiers);
    auto key = textFocus ? editingKey(event.keycode, flags) : KI_UNKNOWN;
    if (first && shortcut) {
        if (event.key == OF_KEY_TAB) key = KI_TAB;
        else if (!textFocus && event.key == OF_KEY_RETURN) key = KI_RETURN;
        else if (!textFocus && event.key == ' ') key = KI_SPACE;
    }
    if (first && key != KI_UNKNOWN) {
        ui.forwardedKeys.insert_or_assign(event.keycode, key);
        ui.context->ProcessKeyDown(key, flags);
    } else if (textFocus && repeatsWhenEditing(key) && ui.forwardedKeys.count(event.keycode)) {
        ui.context->ProcessKeyDown(ui.forwardedKeys.at(event.keycode), flags);
    }
    return true;
}

bool DiagnosticOverlay::pointerEvent(ofMouseEventArgs& event) {
    const bool pressed = event.type == ofMouseEventArgs::Pressed;
    const bool released = event.type == ofMouseEventArgs::Released;
    const glm::vec2 eventPosition{event.x, event.y};
    glm::dvec2 cursorPosition{};
    int physicalButton = event.button;
    if (pressed || released) {
        if (auto* window = dynamic_cast<ofAppGLFWWindow*>(ofGetWindowPtr())) {
            auto* glfwWindow = window->getGLFWWindow();
            if (!open) glfwGetCursorPos(glfwWindow, &cursorPosition.x, &cursorPosition.y);
            // OF may translate either edge of a physical click using the current modifiers.
            unsigned current = 0;
            for (int button = 0; button <= GLFW_MOUSE_BUTTON_LAST; ++button) {
                const unsigned bit = 1u << button;
                const bool down = glfwGetMouseButton(glfwWindow, button) == GLFW_PRESS;
                if (down) current |= bit;
                if (down == pressed && down != static_cast<bool>(physicalButtons & bit))
                    physicalButton = button;
            }
            physicalButtons = current;
        }
    }
    const auto owner = released ? ownedButtons.extract(physicalButton) : decltype(ownedButtons)::node_type{};
    const bool releasedInside = !owner.empty();
    const bool inWindow = event.x >= 0 && event.y >= 0 && event.x < ofGetWidth() && event.y < ofGetHeight();
    if (releasedInside && owner.mapped() == cleanOutputPress) {
        const auto click = std::exchange(cleanClick, std::nullopt);
        // Button events can carry the last motion coordinates instead of the current cursor.
        const bool activateOpener = click && eventPosition == click->eventPosition
            && cursorPosition == click->cursorPosition;
        if (activateOpener && !open && inWindow && event.modifiers == 0)
            show(View::Overview, "overview");
        return true;
    }
    if (!open) {
        if (cleanClick && (pressed || event.type == ofMouseEventArgs::Dragged
            || event.type == ofMouseEventArgs::Exited
            || (event.type == ofMouseEventArgs::Moved && eventPosition != cleanClick->eventPosition)))
            cleanClick.reset();
        if (pressed && inWindow && event.modifiers == 0
            && physicalButton == GLFW_MOUSE_BUTTON_LEFT && physicalButtons == 1u && ownedButtons.empty()) {
            ownedButtons.emplace(physicalButton, cleanOutputPress);
            cleanClick = CleanClick{eventPosition, cursorPosition};
            return true;
        }
        return releasedInside;
    }
    if (event.type == ofMouseEventArgs::Exited) {
        surface->context->ProcessMouseLeave();
        return true;
    }
    const auto size = surface->context->GetDimensions();
    const int x = static_cast<int>(event.x * size.x / std::max(1, ofGetWidth()));
    const int y = static_cast<int>(event.y * size.y / std::max(1, ofGetHeight()));
    const int flags = modifiers(event.modifiers);
    auto& context = *surface->context;
    context.ProcessMouseMove(x, y, flags);
    if (pressed) {
        ownedButtons.insert_or_assign(physicalButton, event.button);
        context.ProcessMouseButtonDown(event.button, flags);
    } else if (releasedInside) {
        context.ProcessMouseButtonUp(owner.mapped(), flags);
    } else if (event.type == ofMouseEventArgs::Scrolled) {
        context.ProcessMouseWheel({-event.scrollX, -event.scrollY}, flags);
    }
    return true;
}

void DiagnosticOverlay::refreshEntries(const Logger& logger) {
    if (cachedSequence != logger.latestSequence()) {
        entries = logger.entriesSince(clearCursor, 512, logGap, &cachedSequence);
        surface->matchesDirty = true;
    }
}

void DiagnosticOverlay::refreshMatches() {
    auto& ui = *surface;
    auto query = ui.search->GetValue();
    if (query != ui.query) {
        ui.query = std::move(query);
        ui.matchesDirty = true;
        ui.filtersCleared = false;
        ui.copyFeedback.clear();
    }
    if (!std::exchange(ui.matchesDirty, false)) return;

    const auto foldedQuery = foldAscii(ui.query);
    ui.matches.clear();
    for (const auto& entry : entries) {
        if (matchesDiagnostic(entry, foldedQuery, ui.errorsOnly))
            ui.matches.push_back(&entry);
    }
}

const LoggerLine* DiagnosticOverlay::diagnostic(uint64_t sequence) const {
    const auto at = std::lower_bound(entries.begin(), entries.end(), sequence,
        [](const auto& entry, uint64_t id) { return entry.sequence < id; });
    return at != entries.end() && at->sequence == sequence ? &*at : nullptr;
}

void DiagnosticOverlay::activate(VOSC& renderer, const Logger& logger, const std::string& id, uint64_t target) {
    const auto& docks = Surface::destinations;
    const auto destination = std::find_if(docks.begin(), docks.end(),
        [&](const auto& dest) { return id == dest.id; });
    if (destination != docks.end()) {
        if (view != destination->view || (view == View::Inspector && surface->inspector.detail())) {
            surface->inspector.reset();
            show(destination->view, id);
        }
        return;
    }
    if (id == "return") close();
    else if (view == View::Browse) {
        auto& ui = *surface;
        if (id == "back" && ui.browse.canGoBack()) {
            show(View::Browse, ui.browse.backItem());
            ui.scroll[static_cast<size_t>(View::Browse)] = ui.browse.originScroll();
        } else if (id.rfind("asset-row-", 0) == 0 && !ui.browse.detail()) {
            if (ui.browse.openItem(id, target, ui.content->GetScrollTop())) {
                show(View::Browse, "back");
                ui.scroll[static_cast<size_t>(View::Browse)] = 0;
            }
        } else if (id == "browse-clear") {
            ui.browseSearch->SetValue("");
            ui.browseCategory->SetSelection(0);
            ui.focusRequest = "browse-search";
        } else if (id == "browse-copy") {
            if (const auto command = ui.browse.copyCommand(renderer, target))
                ui.browse.finishCopy(ui.clipboard.copyBrowseCommand(*command));
        }
    } else if (id == "diagnostic" || id == "shader-diagnostic") showDiagnostic(target);
    else if (id == "back" || id.compare(0, 8, "inspect-") == 0) {
        auto& ui = *surface;
        if (auto nav = ui.inspector.activate(renderer, id, target, ui.content->GetScrollTop(),
                                             view == View::Overview)) {
            const auto next = nav->toOverview ? View::Overview : View::Inspector;
            show(next, nav->focus);
            ui.scroll[static_cast<size_t>(next)] = nav->scroll;
            ui.focusOrigin = nav->origin;
        }
    } else if (id == "errors-only") {
        surface->errorsOnly = !surface->errorsOnly;
        surface->matchesDirty = true;
        surface->filtersCleared = false;
        surface->copyFeedback.clear();
    } else if (id == "follow") {
        surface->following = !surface->following;
        surface->requestTail = surface->following;
    } else if (id == "jump-latest") {
        surface->following = true;
        surface->requestTail = true;
    } else if (id == "copy" || id == "copy-filtered") {
        const auto& matches = surface->matches;
        const auto* selected = diagnostic(selectedSequence);
        std::string clipboardText;
        bool escaped = false;
        if (id == "copy") {
            if (!selected || std::find(matches.begin(), matches.end(), selected) == matches.end()) return;
            appendPasteboardText(clipboardText, selected->message, escaped);
        } else {
            if (matches.empty()) return;
            for (const auto* entry : matches) {
                if (!clipboardText.empty()) clipboardText += '\n';
                clipboardText += "#" + std::to_string(entry->sequence) + " [" + severity(entry->level) + "]";
                if (!entry->module.empty()) {
                    clipboardText += " [";
                    appendPasteboardText(clipboardText, entry->module, escaped);
                    clipboardText += "]";
                }
                clipboardText += " ";
                appendPasteboardText(clipboardText, entry->message, escaped);
            }
        }
        surface->clipboard.SetClipboardText(clipboardText);
        surface->copyFeedback = escaped ? "Copied · NUL escaped" : "Copied";
    } else if (id == "clear") {
        clearCursor = cachedSequence = logger.latestSequence();
        entries.clear();
        logGap = false;
        selectedSequence = 0;
        surface->anchorSequence = 0;
        surface->following = true;
        surface->matchesDirty = true;
        surface->filtersCleared = false;
        surface->copyFeedback.clear();
    } else if (id.compare(0, 4, "log-") == 0) {
        selectedSequence = target;
        surface->filtersCleared = false;
        surface->copyFeedback.clear();
    }
}

std::string DiagnosticOverlay::overviewMarkup(const VOSC& renderer, const GLint* viewport) {
    const auto& layers = renderer.operatorLayers();
    const bool knownScene = !sceneLayers.empty() && layers.size() == sceneLayers.size() &&
        std::equal(layers.begin(), layers.end(), sceneLayers.begin(),
            [](const auto& live, const auto& original) { return live && live == original.layer.lock(); });
    surface->setReading("scene-title", surface->title,
        knownScene && !sceneTitle.empty() ? sceneTitle : "Unnamed scene");
    surface->setReading("render-size", surface->dimensions,
        std::to_string(viewport[2]) + " × " + std::to_string(viewport[3]) + " px");
    const auto camera = renderer.operatorCameraPosition();
    surface->panel->SetClass("camera", camera.has_value());
    if (camera) surface->setReading("camera-position", surface->cameraPosition, ofToString(*camera, 2));
    const auto fps = ofGetFrameRate();
    surface->setReading("observed-fps", surface->fps,
        fps > 0 ? ofToString(fps, 1) + " fps" : "Unavailable");

    const auto latest = std::find_if(entries.rbegin(), entries.rend(),
        [](const auto& entry) { return isError(entry.level); });
    const auto attributed = std::find_if(layers.begin(), layers.end(), [&](const auto& layer) {
        return layer && latest != entries.rend() && layer->reloadDiagnostic() == latest->sequence;
    });
    surface->panel->SetClass("quiet", latest == entries.rend());
    std::string markup;
    const Layer* affected = nullptr;
    if (attributed != layers.end()) {
        affected = attributed->get();
        markup = "<div class='error-summary'>Shader reload failed · Layer " + std::to_string(affected->index)
            + "</div><div class='caption'>The previous shader is still rendering.</div>"
            + surface->inspector.layerLink(*affected, renderer, {}, true);
    } else if (latest != entries.rend()) {
        markup = message(latest->message, true);
    } else {
        markup = "<div class='caption'>No retained errors</div>";
    }
    if (latest != entries.rend()) markup += button("diagnostic", "View diagnostic", latest->sequence);
    for (const auto& layer : layers) {
        if (layer && layer.get() != affected)
            markup += surface->inspector.layerLink(*layer, renderer, knownShaderName(*layer));
    }
    return markup;
}

std::string DiagnosticOverlay::knownShaderName(const Layer& layer) const {
    if (layer.index < 0 || static_cast<size_t>(layer.index) >= sceneLayers.size()) return {};
    const auto& original = sceneLayers[layer.index];
    const auto originalProgram = original.shader.lock();
    if (original.layer.lock().get() != &layer || !originalProgram
        || layer.shader.operatorProgram().lock() != originalProgram
        || !layer.shader.ownsProgram(original.program)) return {};
    return original.asset;
}

std::string DiagnosticOverlay::shaderDiagnostic(const VOSC& renderer) const {
    if (!surface->inspector.isLayerShader()) return {};
    const auto layer = surface->inspector.selectedLayer(renderer);
    if (!layer) return {};
    const auto sequence = layer->reloadDiagnostic();
    if (diagnostic(sequence))
        return message("Reload failed", true)
            + "<div class='caption'>The previous shader is still rendering.</div>"
            + button("shader-diagnostic", "View error in Console", sequence);
    if (sequence > clearCursor) return message("Diagnostic is no longer retained");
    return {};
}

void DiagnosticOverlay::syncConsoleControls() {
    auto& ui = *surface;
    ui.setReading("follow", ui.followLabel, ui.following ? "Following latest" : "Paused");
    ui.document->GetElementById("errors-only")->SetClass("selected", ui.errorsOnly);
    ui.document->GetElementById("copy-filtered")->SetClass("inactive", ui.matches.empty());
    const auto* selected = diagnostic(selectedSequence);
    const bool selectedVisible = selected && std::find(ui.matches.begin(), ui.matches.end(), selected) != ui.matches.end();
    ui.document->GetElementById("copy")->SetClass("inactive", !selectedVisible);

    std::string status;
    auto append = [&](const std::string& text) {
        if (text.empty()) return;
        if (!status.empty()) status += " · ";
        status += text;
    };
    if (ui.filtersCleared) append("Filters cleared to show diagnostic");
    if (selectedVisible) append("Selected diagnostic #" + std::to_string(selectedSequence));
    else if (selected) append("Selected diagnostic hidden by filters");
    else if (selectedSequence) append("Diagnostic is no longer retained");
    if (entries.empty()) append("No retained messages");
    else if (ui.matches.empty()) append("No matching messages");
    else if (logGap && clearCursor && !selectedSequence) append("Older messages expired");
    append(ui.copyFeedback);
    ui.document->GetElementById("console-status")->SetClass("present", !status.empty());
    ui.setReading("console-status", ui.consoleStatus, std::move(status));
}

void DiagnosticOverlay::draw(VOSC& renderer, const Logger& logger) {
    GLint viewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, viewport);
    if (viewport[2] <= 0 || viewport[3] <= 0) return;
    auto& ui = *surface;
    if (view == View::Console && !ui.restoreScroll) ui.observeLogScroll();
    ui.syncViewport(viewport);
    refreshEntries(logger);
    if (view == View::Console) refreshMatches();
    if (view == View::Browse) {
        for (const auto& change : std::exchange(ui.browseChanges, {})) {
            if (change.id == "browse-use") ui.browse.selectUse(change.target);
            else ui.browse.selectTarget(renderer, change.target);
        }
    }
    if (ui.action) {
        const auto action = std::exchange(ui.action, std::nullopt);
        activate(renderer, logger, action->id, action->target);
        if (!open) return;
    }

    const auto* currentFocus = ui.context->GetFocusElement();
    const auto focusId = currentFocus ? currentFocus->GetId() : "";
    const auto focusToken = currentFocus ? currentFocus->GetAttribute<uint64_t>("data-target", 0) : 0;
    const auto rowOrigin = focusToken && ui.inspector.matches(focusId, focusToken) ? focusToken : 0;
    const bool browse = view == View::Browse;
    if (browse && !ui.browse.detail() && ui.browse.setFilters(ui.browseSearch->GetValue(), ui.browseCategory->GetValue())) {
        ui.scroll[static_cast<size_t>(View::Browse)] = 0;
        ui.restoreScroll = true;
    }
    const auto scrollTop = ui.restoreScroll
        ? ui.scroll[static_cast<size_t>(view)] : ui.content->GetScrollTop();
    const bool console = view == View::Console;
    const bool inspector = view == View::Inspector;
    const bool detail = (inspector && ui.inspector.detail()) || (browse && ui.browse.detail());
    const auto& docks = Surface::destinations;
    const auto destination = std::find_if(docks.begin(), docks.end(),
        [&](const auto& dest) { return dest.view == view; });
    ui.setReading("panel-title", ui.viewTitle, inspector ? ui.inspector.title() : destination->title);
    ui.panel->SetClass("overview", view == View::Overview);
    ui.panel->SetClass("browse", browse);
    ui.panel->SetClass("browse-folder", browse && ui.browse.canGoBack());
    ui.panel->SetClass("detail", detail);
    ui.panel->SetClass("inspecting", inspector);
    ui.panel->SetClass("enlarged", inspector && ui.inspector.expanded());
    if (inspector)
        ui.panel->SetProperty("height", std::to_string(ui.inspector.height(renderer, ui.document->IsClassSet("compact"))) + "dp");
    else if (!browse) ui.panel->RemoveProperty("height");
    ui.panel->SetClass("console", console);
    if (!console) ui.document->GetElementById("console-status")->SetClass("present", false);
    for (const auto& dest : docks)
        ui.document->GetElementById(dest.id)->SetClass("selected", dest.view == view);

    std::string markup;
    ui.inspector.beginMarkup();
    switch (view) {
        case View::Overview: markup = overviewMarkup(renderer, viewport); break;
        case View::Inspector: {
            markup = ui.inspector.markup(renderer, shaderDiagnostic(renderer),
                                         [this](const Layer& layer) { return knownShaderName(layer); });
            break;
        }
        case View::Console: break;
        case View::Browse:
            markup = ui.browse.markup(renderer);
            ui.panel->SetProperty("height", std::to_string(ui.browse.height(ui.document->IsClassSet("compact"))) + "dp");
            ui.setReading("browse-heading", ui.browseHeading, ui.browse.heading());
            ui.setReading("browse-summary", ui.browseSummary, ui.browse.summary());
            break;
        case View::Count: break;
    }
    ui.inspector.endMarkup();
    const auto tools = inspector ? ui.inspector.toolbar() : std::string{};
    if (tools != ui.renderedTools) {
        auto* toolbar = ui.document->GetElementById("inspector-tools");
        toolbar->SetClass("present", !tools.empty());
        toolbar->SetInnerRML(tools);
        ui.renderedTools = tools;
    }
    const bool contentChanged = markup != ui.renderedContent;
    ui.syncingBrowse = true;
    if (contentChanged) {
        ui.content->SetInnerRML(markup);
        ui.renderedContent = markup;
    }
    if (inspector) ui.inspector.syncReadings(*ui.content, contentChanged);

    bool rowsChanged = false;
    if (console) {
        refreshMatches();
        syncConsoleControls();
        rowsChanged = ui.syncRows(selectedSequence);
    }

    ui.context->Update();
    ui.syncingBrowse = false;
    auto wantedFocus = std::exchange(ui.focusRequest, {});
    auto origin = std::exchange(ui.focusOrigin, 0);
    if (wantedFocus.empty() && !focusId.empty() &&
        (!ui.context->GetFocusElement() || ui.context->GetFocusElement()->GetId() != focusId ||
         (rowOrigin && !ui.inspector.matches(focusId, rowOrigin)))) {
        wantedFocus = focusId;
        origin = rowOrigin;
    }
    if (!wantedFocus.empty()) {
        auto* element = ui.document->GetElementById(wantedFocus);
        if (!element || element->IsClassSet("inactive") || (origin && !ui.inspector.matches(wantedFocus, origin)))
            element = ui.document->GetElementById(console ? "console-search" : browse && !detail ? "browse-search" : detail ? "back" : destination->id);
        element->Focus(true);
    }
    if (console) ui.placeLogScroll(rowsChanged);
    else if (ui.restoreScroll || contentChanged) ui.content->SetScrollTop(scrollTop);
    ui.restoreScroll = false;

    ui.paint(viewport);
    if (inspector) ui.previews.paint(*ui.content, ui.inspector.previews(), viewport);
}
#endif
