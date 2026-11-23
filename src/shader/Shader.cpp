#include "Shader.h"
#include "../layer/Layer.h"
#include "../var/VariablePool.h"
#include "../light/Lights.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <locale>
#include <optional>
#include <sstream>
#include <regex>
#include <exception>
#include <limits>
#include <stdexcept>
#include <set>
#include "../Camera.hpp"
#include "../utils.h"

void loadShaders(const string& path, map<string, ofxAutoReloadedShader>& shaders) {
    ofDirectory dir(path);
    for (int i = 0; i < dir.getFiles().size(); i++) {
        ofFile file = dir.getFile(i);
        string relPath = path + "/" + file.getFileName();
        if (file.isDirectory() && relPath != "shaders/common") {
            loadShaders(path + "/" + file.getFileName(), shaders);
        }
        else {
            ofxAutoReloadedShader sh;
            string path = file.getAbsolutePath();
            string pathNoExt = path.substr(0, path.find("." + file.getExtension()));
            shaders[pathNoExt] = sh;
        }
    }
}

map<string, ofxAutoReloadedShader> loadShaderCache() {
    map<string, ofxAutoReloadedShader> shaders;
    loadShaders("shaders", shaders);
    return shaders;
}

namespace {

bool hasCompiledStage(const ofShader& shader, GLenum type) {
    const GLuint shaderId = shader.getShader(type);
    if (shaderId == 0) {
        return false;
    }

    GLint status = GL_FALSE;
    glGetShaderiv(shaderId, GL_COMPILE_STATUS, &status);
    return status == GL_TRUE;
}

bool hasLinkedProgram(const ofShader& shader) {
    const GLuint program = shader.getProgram();
    if (program == 0) {
        return false;
    }

    GLint status = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    return status == GL_TRUE;
}

bool isValidShaderProgram(const ofShader& shader, bool hasGeometryStage = false) {
    if (!hasCompiledStage(shader, GL_VERTEX_SHADER) ||
        !hasCompiledStage(shader, GL_FRAGMENT_SHADER)) {
        return false;
    }
#ifndef TARGET_OPENGLES
    if (hasGeometryStage && !hasCompiledStage(shader, GL_GEOMETRY_SHADER_EXT)) {
        return false;
    }
#endif
    return hasLinkedProgram(shader);
}

Shader::Uniforms reflectUniforms(GLuint program) {
    Shader::Uniforms uniforms;
    if (!program) return uniforms;
    GLint count = 0, maximum = 0;
    glGetProgramiv(program, GL_ACTIVE_UNIFORMS, &count);
    glGetProgramiv(program, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maximum);
    if (maximum <= 0) return uniforms;

    vector<GLchar> name(static_cast<size_t>(maximum));
    for (GLint index = 0; index < count; ++index) {
        GLsizei length = 0;
        GLint size = 0;
        GLenum type = 0;
        glGetActiveUniform(program, index, maximum, &length, &size, &type, name.data());
        if (length <= 0) continue;
        string reflected(name.data(), static_cast<size_t>(length));
        const auto bracket = reflected.find('[');
        // The driver reports singleton arrays with a [0] suffix as well.
        const auto key = reflected.substr(0, bracket);
        uniforms.insert_or_assign(key, Shader::UniformInfo{type, size, bracket != string::npos,
                                                          glGetUniformLocation(program, key.c_str())});
    }
    return uniforms;
}

class ValidatedShadertoy : public ofxShadertoy {
public:
    bool hasValidMainShader() const {
        return isValidShaderProgram(mainShader);
    }
};

} // namespace

struct Shader::FileWatch {
    using Stamp = std::optional<std::filesystem::file_time_type>;

    explicit FileWatch(ShaderPaths stages)
        : paths(std::move(stages)), files{ofToDataPath(paths.vertPath, true),
                                        ofToDataPath(paths.fragPath, true),
                                        paths.geomPath.empty() ? "" : ofToDataPath(paths.geomPath, true)},
          stamps(readStamps()), lastCheck(ofGetElapsedTimeMillis()) {}

    std::array<Stamp, 3> readStamps() const {
        std::array<Stamp, 3> result;
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i].empty()) continue;
            std::error_code error;
            const auto stamp = std::filesystem::last_write_time(files[i], error);
            if (!error) result[i] = stamp;
        }
        return result;
    }

    shared_ptr<ofShader> compile() const {
        auto candidate = make_shared<ofShader>();
        const bool hasGeometry = !paths.geomPath.empty();
        if (hasGeometry) {
            if (geometryInput) candidate->setGeometryInputType(*geometryInput);
            if (geometryOutput) candidate->setGeometryOutputType(*geometryOutput);
            if (geometryCount) candidate->setGeometryOutputCount(*geometryCount);
        }
        if (candidate->load(paths.vertPath, paths.fragPath, paths.geomPath) &&
            isValidShaderProgram(*candidate, hasGeometry)) return candidate;
        return nullptr;
    }

    const ShaderPaths paths;
    const std::array<std::filesystem::path, 3> files;
    std::array<Stamp, 3> stamps;
    uint64_t lastCheck;
    std::optional<GLenum> geometryInput;
    std::optional<GLenum> geometryOutput;
    std::optional<int> geometryCount;
    ofEventListener listener;
};

Shader::Shader() : VarsHolder(), texLoc(0) {}

Shader::~Shader() {
    reset();
}

#if VOSC_OPERATOR_UI
std::string Shader::operatorSourcePath() const {
    return fileWatch ? fileWatch->paths.fragPath : shaderPath;
}
#endif

void Shader::setShaderWatchEnabled(bool enabled) {
    if (!fileWatch) return;
    if (!enabled) {
        fileWatch->listener.unsubscribe();
        return;
    }
    fileWatch->listener = ofEvents().update.newListener([this](ofEventArgs&) {
        auto& watch = *fileWatch;
        const auto now = ofGetElapsedTimeMillis();
        if (now - watch.lastCheck < 2000) return;
        watch.lastCheck = now;
        const auto stamps = watch.readStamps();
        if (stamps == watch.stamps) return;
        watch.stamps = stamps;

        // Keep the installed program alive until all candidate stages and its link are valid.
        auto candidate = watch.compile();
        if (!candidate) {
            auto diagnostic = ofLogError("Shader");
            diagnostic << "Watched shader reload failed for " << watch.files[1].string()
                       << " (vertex " << watch.files[0].string();
            if (!watch.files[2].empty()) diagnostic << ", geometry " << watch.files[2].string();
            diagnostic << "). Previous shader is still rendering.";
            return;
        }
        shader = std::move(candidate);
        clearUniforms();
    });
}

map<string, ofxAutoReloadedShader>& Shader::shaderCache() {
    static auto cache = loadShaderCache();
    return cache;
}

string Shader::random() {
    const auto& cache = shaderCache();
    if (cache.empty()) return {};
    auto it = cache.begin();
    advance(it, int(ofRandom(cache.size())));
    return it->first;
}

bool isSource(const string& path) {
    static const std::regex mainFunction("\\bvoid\\s+main\\s*\\(");
    return std::regex_search(path, mainFunction);
}

string parseShadertoy(const string& path) {
    std::regex re_view("^https?://(www\\.)?shadertoy\\.com/view/(.+)(/|\\?.*)?");
    std::regex re_api("^https?://(www\\.)?shadertoy\\.com/api/v1/shaders/(.+)(/|\\?.*)");
    std::smatch m;
    if (regex_match(path, m, re_view) && m.size() >= 3) {
        std::ssub_match submatch = m[2];
        return submatch.str();
    }
    else if (regex_match(path, m, re_api) && m.size() >= 3) {
        std::ssub_match submatch = m[2];
        return submatch.str();
    }
    return "";
}

bool Shader::load(string path) {
    if (path.empty()) {
        unload();
        return false;
    }

    bool success = false;
    bool _isSource = isSource(path);
    string shadertoyId = parseShadertoy(path);
    bool _isFile = !_isSource && shadertoyId == "";
    if (_isSource) {
        success = loadFromSource(path);
        if (!success) {
            ofLogError() << ("could not load shader from source");
        }
    }
    else if (shadertoyId != "") {
        success = loadShadertoy(shadertoyId);
        if (!success) {
            ofLogError() << ("could not load shadertoy: " + shadertoyId);
        }
    }
    else if (_isFile) {
        success = loadFromFileAndWatch(path);
        if (!success) {
            ofLogError() << ("could not load shader: " + path);
        }
    }
    return success;
}

bool Shader::loadFromSource(const string& source) {
    PreparedDirectSource prepared;
    if (!prepareDirectSource(source, prepared)) return false;
    shader = std::move(prepared.program);
    shadertoy.reset();
    shaderPath = "source";
    directSource = prepared.source;
    fileWatch.reset();
    // Preserve existing VarsHolder/VariablePool bindings. Ordinary source
    // replacement has always kept them; detached bindings are trial-only.
    uniforms = std::move(prepared.uniforms);
    uniformProgram = shader->getProgram();
    return true;
}

bool Shader::prepareDirectSource(const string& source, PreparedDirectSource& prepared) {
    ofShaderSettings settings;
    settings.shaderFiles[GL_VERTEX_SHADER] = "shaders/passthru.vert";
    settings.shaderSources[GL_FRAGMENT_SHADER] = source;
    shared_ptr<ofShader> candidate = make_shared<ofShader>();
    if (!candidate->setup(settings) || !isValidShaderProgram(*candidate)) {
        return false;
    }
    prepared.program = std::move(candidate);
    prepared.source = source;
    prepared.uniforms = reflectUniforms(prepared.program->getProgram());
    return true;
}

bool Shader::snapshotDirectSource(DirectSourceState& state, string& reason) const {
    if (!isShaderLoaded() || shadertoy || fileWatch) {
        reason = "baseline shader must be a loaded direct-source program";
        return false;
    }
    if (!textures.empty() || !buffers.empty()) {
        reason = "baseline shader has texture or buffer bindings";
        return false;
    }
    if (vars.size() > 64) {
        reason = "baseline has more than 64 numeric bindings";
        return false;
    }
    DirectSourceState candidate;
    candidate.program = shader;
    candidate.path = shaderPath;
    candidate.source = directSource;
    for (const auto& binding : vars) {
        if (binding.first.empty() || binding.first.size() > 128) {
            reason = "baseline binding name is outside supported bounds";
            return false;
        }
        auto value = dynamic_pointer_cast<Variable<float>>(binding.second);
        if (!value || !value->isConst() || value->size() < 1 || value->size() > 4) {
            reason = "baseline bindings must be constant float vectors of one to four components";
            return false;
        }
        // A normal holder-local binding is owned by Shader::vars and the
        // VariablePool holder map (plus this temporary reference). Reject
        // aliases rather than changing another holder's binding semantics.
        if (value.use_count() > 3) {
            reason = "aliased baseline bindings are outside protected-trial scope";
            return false;
        }
        for (float component : value->getVec()) {
            if (!std::isfinite(component) || component < -1000000.f || component > 1000000.f) {
                reason = "baseline binding component is outside supported bounds";
                return false;
            }
        }
        candidate.bindings[binding.first] = value->getVec();
    }
    state = std::move(candidate);
    return true;
}

void Shader::installDirectSource(PreparedDirectSource&& prepared,
                                 const map<string, vector<float>>& bindings) {
    auto& pool = VariablePool::getPool(this);
    shader = std::move(prepared.program);
    shadertoy.reset();
    shaderPath = "source";
    directSource = prepared.source;
    fileWatch.reset();
    uniforms = std::move(prepared.uniforms);
    uniformProgram = shader->getProgram();
    vars.clear();
    pool.clear();
    for (const auto& binding : bindings) {
        auto value = make_shared<Variable<float>>();
        value->set(binding.second);
        vars[binding.first] = value;
        pool[binding.first] = value;
    }
}

bool Shader::restoreDirectSource(const DirectSourceState& state) {
    if (!state.program || !state.program->isLoaded()) return false;
    auto& pool = VariablePool::getPool(this);
    shader = state.program;
    shadertoy.reset();
    shaderPath = state.path;
    directSource = state.source;
    fileWatch.reset();
    clearUniforms();
    vars.clear();
    pool.clear();
    for (const auto& binding : state.bindings) {
        auto value = make_shared<Variable<float>>();
        value->set(binding.second);
        vars[binding.first] = value;
        pool[binding.first] = value;
    }
    return true;
}

bool Shader::ownsProgram(GLuint program) const {
    return isShaderLoaded() && shader->getProgram() == program;
}

bool Shader::loadShadertoy(const string& shadertoyId) {
    try {
        ofHttpResponse resp;
#ifdef VOSC_RECOVERY_TESTING
        if (shadertoyId == "VoscOfflineFixture") {
            const char* directory = std::getenv("VOSC_RECOVERY_TEST_DIR");
            if (!directory || !*directory) return false;
            resp.data = ofBufferFromFile(string(directory) + "/shadertoy-api.json");
            resp.status = resp.data.size() ? 200 : 404;
        } else
#endif
        {
            resp = ofLoadURL("https://www.shadertoy.com/api/v1/shaders/" + shadertoyId + "?key=ft8tMh");
        }
        if (resp.status != 200) {
            return false;
        }

        ofJson json = ofJson::parse(resp.data.getText());
        const string source = json["Shader"]["renderpass"][0]["code"].get<string>();
        string fragPath = "shaders/shadertoy/" + shadertoyId + ".frag";
        ofFile shaderFile;
        if (!shaderFile.open(fragPath, ofFile::WriteOnly)) {
            ofLogError() << ("could not write shadertoy shader: " + fragPath);
            return false;
        }
        ofBuffer sourceBuffer;
        sourceBuffer.set(source);
        if (!shaderFile.writeFromBuffer(sourceBuffer)) {
            ofLogError() << ("could not write shadertoy shader: " + fragPath);
            shaderFile.close();
            return false;
        }
        shaderFile.close();

        shared_ptr<ValidatedShadertoy> candidate = make_shared<ValidatedShadertoy>();
        if (!candidate->load(fragPath) || !candidate->hasValidMainShader()) {
            return false;
        }
        candidate->setUseAutoUpdate(true);

        shadertoy = candidate;
        shader.reset();
        shaderPath = "shadertoy/" + shadertoyId + ".frag";
        directSource.clear();
        fileWatch.reset();
        clearUniforms();
        return true;
    }
    catch (const std::exception& ex) {
        ofLogError() << ("could not load shadertoy: " + shadertoyId + " (" + ex.what() + ")");
        return false;
    }
}

ShaderPaths Shader::getShaderPaths(const string& path) {
    ShaderPaths paths;
    string fragPath = "";
    vector<string> fragPaths;
    if (!ofFilePath::isAbsolute(path)) {
        fragPaths.insert(fragPaths.end(), {
            ofToDataPath(path),
            ofToDataPath(path + ".frag"),
            ofToDataPath("shaders/" + path),
            ofToDataPath("shaders/" + path + ".frag"),
        });
    }
    else {
        fragPaths.insert(fragPaths.end(), {
            path,
            path + ".frag",
        });
    }
    for (int i=0; i<fragPaths.size(); i++) {
        if (ofFile(fragPaths[i]).exists()) {
            fragPath = fragPaths[i];
            break;
        }
    }
    
    if (fragPath != "") {
        paths.fragPath = fragPath;
        paths.vertPath = "shaders/passthru.vert";
        paths.geomPath = "";
        
        string pathNoExt = fragPath.substr(0, fragPath.find(".frag"));
        string tmpVertPath = pathNoExt + ".vert";
        if (ofFile(tmpVertPath).exists()) {
            paths.vertPath = tmpVertPath;
        }
        string tmpGeomPath = pathNoExt + ".geom";
        if (ofFile(tmpGeomPath).exists()) {
            paths.geomPath = tmpGeomPath;
        }
    }
    
    return paths;
}

bool Shader::loadNativeStages(const ShaderPaths& paths) {
    auto candidate = std::make_shared<ofShader>();
    if (!candidate->load(paths.vertPath, paths.fragPath, paths.geomPath) ||
        !isValidShaderProgram(*candidate, !paths.geomPath.empty()))
        return false;
    shader = std::move(candidate);
    shadertoy.reset();
    shaderPath = ofFilePath::getFileName(paths.fragPath);
    directSource.clear();
    fileWatch.reset();
    clearUniforms();
    return true;
}

bool Shader::loadFromFileAndWatch(const string& path) {
    ShaderPaths paths = getShaderPaths(path);
    if (!paths.isValid()) return false;
    auto watch = std::make_unique<FileWatch>(paths);
    auto candidate = watch->compile();
    if (!candidate) return false;

    const bool exportableStages = paths.geomPath.empty() && paths.vertPath == "shaders/passthru.vert";
    ofBuffer retained = exportableStages ? ofBufferFromFile(paths.fragPath, true) : ofBuffer();
    shader = std::move(candidate);
    shadertoy.reset();
    shaderPath = ofFilePath::getFileName(paths.fragPath);
    directSource = retained.size() > 0 && retained.size() <= 128 * 1024 ? retained.getText() : "";
    fileWatch = std::move(watch);
    setShaderWatchEnabled(true);
    clearUniforms();
    return true;
}

void Shader::update(const vector<TidalNote> &notes) {
    for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
        it->second->update();
    }
}

namespace {
struct OrdinaryIntegerVector : Variable<int> {
    explicit OrdinaryIntegerVector(size_t arity) : arrayArity(arity) {}
    const size_t arrayArity;
};

size_t integerVectorArity(GLenum type) {
    switch (type) {
        case GL_INT_VEC2: return 2;
        case GL_INT_VEC3: return 3;
        case GL_INT_VEC4: return 4;
        default: return 0;
    }
}

size_t floatVectorArity(GLenum type) {
    switch (type) {
        case GL_FLOAT_VEC2: return 2;
        case GL_FLOAT_VEC3: return 3;
        case GL_FLOAT_VEC4: return 4;
        default: return 0;
    }
}

template<typename T>
shared_ptr<BaseVar> makeFloatVector(const vector<Shader::FloatComponent>& components, const T& constant) {
    auto variable = std::make_shared<Variable<T>>();
    const bool hasExpression = std::any_of(components.begin(), components.end(), [](const auto& component) {
        return std::holds_alternative<string>(component);
    });
    if (!hasExpression) {
        variable->set(constant);
        return variable;
    }

    vector<string> values;
    vector<bool> expressions;
    values.reserve(components.size());
    expressions.reserve(components.size());
    for (const auto& component : components) {
        if (const auto* expression = std::get_if<string>(&component)) {
            values.push_back(*expression);
            expressions.push_back(true);
        } else {
            // Format the promoted float so the mixed setter's double parse preserves its bits.
            std::ostringstream literal;
            literal.imbue(std::locale::classic());
            literal << std::setprecision(std::numeric_limits<double>::max_digits10)
                    << static_cast<double>(std::get<float>(component));
            values.push_back(literal.str());
            expressions.push_back(false);
        }
    }

    try {
        variable->set(values, expressions);
    } catch (const mu::Parser::exception_type&) {
        throw std::invalid_argument("could not register float uniform expression dependencies");
    }
    if (!variable->validateComponentExpressions())
        throw std::invalid_argument("could not initialize float uniform expression");
    return variable;
}

}

GLenum Shader::reflectedNonArrayUniformType(const string& name) const {
    const auto& reflected = getUniforms();
    const auto found = reflected.find(name);
    if (found == reflected.end() || found->second.isArray() ||
        glGetUniformLocation(uniformProgram, name.c_str()) < 0)
        return 0;
    return found->second.type;
}

size_t Shader::reflectedIntegerVectorArity(const string& name) const {
    return integerVectorArity(reflectedNonArrayUniformType(name));
}

size_t Shader::reflectedFloatVectorArity(const string& name) const {
    return floatVectorArity(reflectedNonArrayUniformType(name));
}

bool Shader::handleLayerVectorWrite(const ofxOscMessage& message, int layerIndex) {
    if (message.getNumArgs() != 3 || message.getArgType(1) != OFXOSC_TYPE_STRING) return false;
    const auto payloadType = message.getArgType(2);
    const bool isScalar = payloadType == OFXOSC_TYPE_INT32 || payloadType == OFXOSC_TYPE_FLOAT;
    string payload;
    if (!isScalar) {
        if (payloadType != OFXOSC_TYPE_STRING) return false;
        payload = message.getArgAsString(2);
        const auto first = payload.find_first_not_of(" \t\r\n");
        if (first == string::npos || payload[first] != '[') return false;
    }

    const auto name = message.getArgAsString(1);
    const auto type = reflectedNonArrayUniformType(name);
    const size_t arity = integerVectorArity(type);
    const size_t floatArity = floatVectorArity(type);
    if (!arity && !floatArity) return false;
    auto reject = [&](const char* reason) {
        ofLogError() << message.getAddress() << " rejected layer " << layerIndex << " uniform "
                     << name << ": " << reason;
        return true;
    };

    if (isScalar) {
        if (floatArity) {
            const float value = payloadType == OFXOSC_TYPE_FLOAT ? message.getArgAsFloat(2)
                : static_cast<float>(message.getArgAsInt32(2));
            if (!std::isfinite(value)) return reject("expected finite float32 scalar");
            auto variable = std::make_shared<Variable<float>>();
            variable->set(vector<float>(floatArity, value));
            publishUniformBinding(name, variable);
        } else {
            if (payloadType != OFXOSC_TYPE_INT32) return reject("expected int32 scalar");
            setIntegerVector(name, arity, vector<IntegerComponent>(arity, message.getArgAsInt32(2)));
        }
        return true;
    }

    if (floatArity && payload.find('\0') != string::npos) return reject("invalid array syntax");
    ofJson array;
    try {
        array = ofJson::parse(payload);
    } catch (const ofJson::out_of_range&) {
        return reject(floatArity ? "component outside float32 range" : "component outside int32 range");
    } catch (const ofJson::parse_error&) {
        return reject("invalid array syntax");
    }
    if (floatArity) {
        bool rgbShorthand = false;
        if (floatArity == 4 && array.size() == 3) {
            const auto found = vars.find(name);
            if (found != vars.end()) {
                const auto* previous = dynamic_cast<const OrdinaryIntegerVector*>(found->second.get());
                rgbShorthand = dynamic_cast<const Variable<ofFloatColor>*>(found->second.get()) ||
                               (previous && previous->arrayArity == 4);
            }
        }
        if (!array.is_array() || (array.size() != floatArity && !rgbShorthand))
            return reject("incorrect vector arity");

        vector<FloatComponent> components;
        components.reserve(floatArity);
        bool allNumeric = true;
        bool normalizeRgb = false;
        for (const auto& field : array) {
            if (field.is_string() && !field.get_ref<const string&>().empty()) {
                components.emplace_back(field.get<string>());
                allNumeric = false;
                continue;
            }

            float rounded;
            if (field.is_number_unsigned()) {
                rounded = static_cast<float>(field.get<std::uint64_t>());
            } else if (field.is_number_integer()) {
                rounded = static_cast<float>(field.get<std::int64_t>());
            } else if (field.is_number_float()) {
                const double value = field.get<double>();
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                    return reject("component outside float32 range");
                rounded = static_cast<float>(value);
            } else {
                return reject("expected float32 or expression components");
            }
            components.emplace_back(rounded);
            normalizeRgb = normalizeRgb || rounded > 1.f;
        }
        if (rgbShorthand) {
            if (allNumeric && normalizeRgb) {
                for (auto& component : components) std::get<float>(component) /= 255.f;
            }
            components.emplace_back(1.f);
        }

        try {
            setFloatVector(name, floatArity, components);
        } catch (const std::exception& error) {
            return reject(error.what());
        }
        return true;
    }
    if (!array.is_array() || array.size() != arity) return reject("incorrect vector arity");

    vector<IntegerComponent> components;
    components.reserve(arity);
    for (const auto& field : array) {
        if (field.is_string() && !field.get_ref<const string&>().empty()) {
            components.emplace_back(field.get<string>());
        } else if (field.is_number_unsigned()) {
            const auto value = field.get<std::uint64_t>();
            if (value > std::numeric_limits<std::int32_t>::max())
                return reject("component outside int32 range");
            components.emplace_back(static_cast<std::int32_t>(value));
        } else if (field.is_number_integer()) {
            const auto value = field.get<std::int64_t>();
            if (value < std::numeric_limits<std::int32_t>::min() ||
                value > std::numeric_limits<std::int32_t>::max())
                return reject("component outside int32 range");
            components.emplace_back(static_cast<std::int32_t>(value));
        } else {
            return reject("expected int32 or expression components");
        }
    }
    try {
        setIntegerVector(name, arity, components, std::make_shared<OrdinaryIntegerVector>(arity));
    } catch (...) {
        return reject("could not initialize integer vector");
    }
    return true;
}

void Shader::setFloatVector(const string& name, size_t arity, const vector<FloatComponent>& components) {
    if (arity < 2 || arity > 4 || components.size() != arity)
        throw std::invalid_argument("incorrect float uniform vector arity");

    float values[4] = {};
    for (size_t index = 0; index < arity; ++index) {
        if (const auto* value = std::get_if<float>(&components[index])) {
            if (!std::isfinite(*value))
                throw std::invalid_argument("component outside float32 range");
            values[index] = *value;
        }
    }

    shared_ptr<BaseVar> variable;
    if (arity == 2)
        variable = makeFloatVector(components, glm::vec2(values[0], values[1]));
    else if (arity == 3)
        variable = makeFloatVector(components, glm::vec3(values[0], values[1], values[2]));
    else
        variable = makeFloatVector(components, ofFloatColor(values[0], values[1], values[2], values[3]));
    publishUniformBinding(name, variable);
}

void Shader::setIntegerVector(const string& name, size_t arity, const vector<IntegerComponent>& components) {
    setIntegerVector(name, arity, components, std::make_shared<Variable<int>>());
}

void Shader::setIntegerVector(const string& name, size_t arity, const vector<IntegerComponent>& components,
                              const shared_ptr<Variable<int>>& variable) {
    if (arity < 2 || arity > 4 || components.size() != arity)
        throw std::invalid_argument("incorrect integer uniform vector arity");

    const bool allIntegers = std::all_of(components.begin(), components.end(), [](const auto& component) {
        return std::holds_alternative<std::int32_t>(component);
    });
    if (allIntegers) {
        vector<int> values;
        values.reserve(arity);
        for (const auto& component : components) values.push_back(std::get<std::int32_t>(component));
        variable->set(std::move(values));
    } else {
        vector<string> values;
        vector<bool> expressions;
        values.reserve(arity);
        expressions.reserve(arity);
        for (const auto& component : components) {
            const auto* expression = std::get_if<string>(&component);
            expressions.push_back(expression != nullptr);
            values.push_back(expression ? *expression : std::to_string(std::get<std::int32_t>(component)));
        }
        variable->set(values, expressions);
        if (!variable->validateComponentExpressions())
            throw std::invalid_argument("could not initialize integer uniform expression");
    }
    publishUniformBinding(name, variable);
}

void Shader::publishUniformBinding(const string& name, const shared_ptr<BaseVar>& variable) {
    auto& pool = VariablePool::getPool(this);
    const auto poolEntry = pool.try_emplace(name);
    try {
        const auto shaderEntry = vars.try_emplace(name).first;
        shaderEntry->second = variable;
        poolEntry.first->second = variable;
    } catch (...) {
        if (poolEntry.second) pool.erase(poolEntry.first);
        throw;
    }
}

void Shader::oscCommand(const string& command, const ofxOscMessage& m) {
    if (command == "/shader") {
        load(m.getArgAsString(1));
    }
    else if (command == "/shader/set") {
        set(m);
    }
    else if (command == "/shader/var" || command == "/shader/uniform") {
        string name = m.getArgAsString(1);
        setVar(name, m, 2);
    }
    else if (command == "/shader/texture" || command == "/shader/tex") {
        setTexture(m);
    }
    else if (command == "/shader/texture/size" || command == "/shader/texture/speed" ||
             command == "/shader/texture/set") {
        const string name = m.getArgAsString(1);
        auto found = textures.find(name);
        if (found == textures.end() || !found->second) {
            ofLogError() << "named texture not found: " << name;
            return;
        }
        auto& texture = *found->second;
        if (command == "/shader/texture/size") {
            texture.data.setExplicitSize(m.getArgAsFloat(2), m.getArgAsFloat(3));
        } else if (command == "/shader/texture/speed") {
            texture.setVar("speed", m, 2);
        } else {
            const string property = m.getArgAsString(2);
            const int value = m.getArgAsInt(3);
            if (property == "aspectRatio") texture.data.aspectRatio = value != 0;
            else if (property == "static") texture.isStatic = value != 0;
            else if (property == "blendMode") texture.data.blendMode = static_cast<ofBlendMode>(value);
        }
    }
    else if (command == "/shader/buffer" || command == "/shader/buf") {
        setBuffer(m);
    }
    else if (command == "/shader/reset") {
        reset();
    }
}

bool Shader::isRendererUniform(const string& name) {
    return name == "time" || name == "resolution" || name == "random" ||
           name == "numLights" || name == "lights" || name == "offset" || name == "layer" ||
           name == "globalColor" || name == "usingTexture" || name == "usingColors" ||
           name == "projectionMatrix" || name == "modelViewMatrix" ||
           name == "modelViewProjectionMatrix" || name == "textureMatrix";
}

const char* Shader::sceneUniformError(const Uniforms& uniforms, const string& name) {
    if (name == "layer" || name == "offset" || name == "numLights") return "renderer-owned shader uniform";
    const auto found = uniforms.find(name);
    if (found == uniforms.end()) return "shader binding is absent from the active program";
    const auto& info = found->second;
    // Layer data uploads follow explicit layer-shader bindings in begin().
    static const std::set<string> layerScalars = {"alpha", "bri", "speed", "visible", "visibleThresh", "onsetThresh"};
    static const std::set<string> layerVectors = {"pos", "size", "rotation", "pivot", "scale"};
    const bool layerOwned =
        ((info.type == GL_FLOAT || info.type == GL_INT) && layerScalars.count(name)) ||
        ((info.type == GL_FLOAT_VEC2 || info.type == GL_FLOAT_VEC3) && layerVectors.count(name)) ||
        ((info.type == GL_FLOAT_VEC3 || info.type == GL_FLOAT_VEC4) && name == "tint");
    if (layerOwned) return "layer-owned shader uniform";
    if (info.isArray() && !(info.size == 1 && info.type == GL_FLOAT))
        return "unsupported shader uniform array";
    if (info.location < 0) return "shader uniform has no writable location";
    return nullptr;
}

std::string Shader::sceneTextureError(const Uniforms& uniforms, const string& name,
                                      bool explicitSize) {
    const auto writable = [&](const string& key, GLenum type) {
        const auto found = uniforms.find(key);
        return found == uniforms.end() || (found->second.type == type && !found->second.isArray() &&
                                           found->second.location >= 0);
    };
    if (!writable(name, GL_SAMPLER_2D)) return "expected a writable sampler2D: " + name;
    if (!explicitSize && !writable(name + "Size", GL_FLOAT_VEC2))
        return "renderer-managed texture size requires a writable vec2: " + name + "Size";
    return {};
}

void Shader::begin(TexData& data, int delay, const Camera* camera) {
    if (isLoaded()) {
        // Reset texture location at the start of each begin call
        texLoc = 0;
        
        glm::vec2 size = data.getSize();
        auto beginShader = [&data, &size](auto& sh) {
            sh.begin();
            sh.setUniform1f("time", data.time);
            sh.setUniform2f("resolution", size.x, size.y);
            sh.setUniform1i("random", data.randomSeed);
        };
        
        if (shadertoy == NULL) {
            beginShader(*shader);
            // todo: maybe set only for Layer shaders?
            if (camera != nullptr && camera->isEnabled()) {
                setUniformCameraMatrices(shader, camera->getCamera());
            }
        }
        else {
            beginShader(*shadertoy);
        }
        setUniformTextures(textures, delay);
        setUniforms(vars);
        setUniforms(data.getVars());
        if (shadertoy == NULL) {
            setLights(shader);
        }
        else {
            setLights(shadertoy);
        }
    }
}

void Shader::end() {
    if (isLoaded()) {
        if (shadertoy == NULL) {
            shader->end();
        }
        else {
            shadertoy->end();
        }
    }
}

void Shader::setUniformTextureWithSize(const string& name, ofTexture& tex, int loc) {
    setUniformTexture(name, tex, loc >= 0 ? loc : getNextTextureLocation());
    const auto sizeName = name + "Size";
    if (shadertoy == NULL) {
        // An explicitly bound scalar can use this name. Avoid the incompatible
        // automatic vec2 write before uploading the scalar in setUniforms().
        if (vars.count(sizeName)) {
            const auto& types = getUniforms();
            const auto found = types.find(sizeName);
            if (found != types.end() && (found->second.type != GL_FLOAT_VEC2 || found->second.isArray()))
                return;
        }
        shader->setUniform2f(sizeName, tex.getWidth(), tex.getHeight());
    }
    else {
        shadertoy->setUniform2f(sizeName, tex.getWidth(), tex.getHeight());
    }
}

int Shader::getNextTextureLocation() {
    return texLoc++;
}

void Shader::setUniformTextures(const map<string, shared_ptr<Texture>>& textures, int delay) {
    for (map<string, shared_ptr<Texture>>::const_iterator it=textures.begin(); it!=textures.end(); ++it) {
        shared_ptr<Texture> tex = it->second;
        if (tex->hasTexture(delay)) {
            for (int i=0; i<tex->getNumTextures(); i++) {
                string name = i == 0 ? it->first : it->first + ofToString(i);
                ofTexture& texture = const_cast<ofTexture&>(tex->getTexture(delay, i));
                this->setUniformTextureWithSize(name, texture);
            }
        }
    }
    for (map<string, shared_ptr<Buffer>>::const_iterator it=buffers.begin(); it!=buffers.end(); ++it) {
        ofTexture& texture = const_cast<ofTexture&>(it->second->getTexture());
        this->setUniformTextureWithSize(it->first, texture);
    }
}

void Shader::setUniforms(const map<string, shared_ptr<BaseVar>>& vars) {
    if (shadertoy == NULL) {
        setUniforms(shader, vars);
    }
    else {
        setUniforms(shadertoy, vars);
    }
}

template<typename T>
void Shader::setUniformFloatArray(shared_ptr<T>& shader, const string& name, const vector<float>& values) {
    if (values.size() == 1) {
        shader->setUniform1f(name, values[0]);
    }
    else if (values.size() == 2) {
        shader->setUniform2f(name, values[0], values[1]);
    }
    else if (values.size() == 3) {
        shader->setUniform3f(name, values[0], values[1], values[2]);
    }
    else if (values.size() == 4) {
        shader->setUniform4f(name, values[0], values[1], values[2], values[3]);
    }
    else {
        shader->setUniform1fv(name, &values[0], values.size());
    }
}

template<typename T>
void Shader::setUniformIntArray(shared_ptr<T>& shader, const string& name, const vector<int>& values) {
    if (values.size() == 1) {
        shader->setUniform1i(name, values[0]);
    }
    else if (values.size() == 2) {
        shader->setUniform2i(name, values[0], values[1]);
    }
    else if (values.size() == 3) {
        shader->setUniform3i(name, values[0], values[1], values[2]);
    }
    else if (values.size() == 4) {
        shader->setUniform4i(name, values[0], values[1], values[2], values[3]);
    }
    else {
        shader->setUniform1iv(name, &values[0], values.size());
    }
}

template<typename T>
void Shader::setUniformFromFloatVar(shared_ptr<T>& shader, const string& name, const Variable<float>* var, GLenum uniformType) {
    if (var == NULL) return;
    const auto& values = var->getVec();
    
    switch (uniformType) {
        case GL_FLOAT_VEC2:
            if (values.size() >= 2) {
                shader->setUniform2f(name, values[0], values[1]);
            }
            break;
        case GL_FLOAT_VEC3:
            if (values.size() >= 3) {
                shader->setUniform3f(name, values[0], values[1], values[2]);
            }
            break;
        case GL_FLOAT_VEC4:
            if (values.size() >= 4) {
                shader->setUniform4f(name, values[0], values[1], values[2], values[3]);
            }
            break;
        default:
            setUniformFloatArray(shader, name, values);
            break;
    }
}

// Helper function to get dimension from GLenum uniform type
static int getUniformTypeDimension(GLenum uniformType) {
    switch (uniformType) {
        case GL_FLOAT_VEC2:
        case GL_INT_VEC2:
            return 2;
        case GL_FLOAT_VEC3:
        case GL_INT_VEC3:
            return 3;
        case GL_FLOAT_VEC4:
        case GL_INT_VEC4:
            return 4;
        default:
            return 0;
    }
}

// Helper function to try setting vec2 uniform from a glm vector variable
// Can use vec2, vec3, or vec4 (takes first 2 components)
template<typename T>
static bool trySetUniformVec2(shared_ptr<T>& shader, const string& name, shared_ptr<const BaseVar> var) {
    const auto vec2Var = std::dynamic_pointer_cast<const Variable<glm::vec2>>(var);
    if (vec2Var != NULL && glmVecTypeDimensionOf(vec2Var->get()) >= 2) {
        glm::vec2 vec = vec2Var->get();
        shader->setUniform2f(name, vec.x, vec.y);
        return true;
    }
    const auto vec3Var = std::dynamic_pointer_cast<const Variable<glm::vec3>>(var);
    if (vec3Var != NULL && glmVecTypeDimensionOf(vec3Var->get()) >= 2) {
        glm::vec3 vec = vec3Var->get();
        shader->setUniform2f(name, vec.x, vec.y);
        return true;
    }
    const auto vec4Var = std::dynamic_pointer_cast<const Variable<glm::vec4>>(var);
    if (vec4Var != NULL && glmVecTypeDimensionOf(vec4Var->get()) >= 2) {
        glm::vec4 vec = vec4Var->get();
        shader->setUniform2f(name, vec.x, vec.y);
        return true;
    }
    return false;
}

// Helper function to try setting vec3 uniform from a glm vector variable
// Can use vec3 or vec4 (takes first 3 components)
template<typename T>
static bool trySetUniformVec3(shared_ptr<T>& shader, const string& name, shared_ptr<const BaseVar> var) {
    const auto vec3Var = std::dynamic_pointer_cast<const Variable<glm::vec3>>(var);
    if (vec3Var != NULL && glmVecTypeDimensionOf(vec3Var->get()) >= 3) {
        shader->setUniform3f(name, vec3Var->get());
        return true;
    }
    const auto vec4Var = std::dynamic_pointer_cast<const Variable<glm::vec4>>(var);
    if (vec4Var != NULL && glmVecTypeDimensionOf(vec4Var->get()) >= 3) {
        glm::vec4 vec = vec4Var->get();
        shader->setUniform3f(name, vec.x, vec.y, vec.z);
        return true;
    }
    const auto colorVar = std::dynamic_pointer_cast<const Variable<ofFloatColor>>(var);
    if (colorVar != NULL) {
        ofFloatColor c = colorVar->get();
        shader->setUniform3f(name, c.r, c.g, c.b);
        return true;
    }
    return false;
}

// Helper function to try setting vec4 uniform from a glm vector variable
// Can only use vec4 or ofFloatColor
template<typename T>
static bool trySetUniformVec4(shared_ptr<T>& shader, const string& name, shared_ptr<const BaseVar> var) {
    const auto vec4Var = std::dynamic_pointer_cast<const Variable<glm::vec4>>(var);
    if (vec4Var != NULL && glmVecTypeDimensionOf(vec4Var->get()) == 4) {
        shader->setUniform4f(name, vec4Var->get());
        return true;
    }
    const auto colorVar = std::dynamic_pointer_cast<const Variable<ofFloatColor>>(var);
    if (colorVar != NULL) {
        shader->setUniform4f(name, colorVar->get());
        return true;
    }
    return false;
}

template<typename T>
void Shader::setUniformFromIntVar(shared_ptr<T>& shader, const string& name, const Variable<int>* var, GLenum uniformType) {
    if (var == NULL) return;
    const auto& values = var->getVec();
    
    switch (uniformType) {
        case GL_INT:
        case GL_INT_VEC2:
        case GL_INT_VEC3:
        case GL_INT_VEC4:
            setUniformIntArray(shader, name, values);
            break;
        default:
            if (values.size() >= 1) {
                shader->setUniform1i(name, values[0]);
            }
            break;
    }
}

template<typename T>
void Shader::setUniformFromFloatFromInt(shared_ptr<T>& shader, const string& name, const Variable<int>* var, GLenum uniformType) {
    if (var == NULL) return;
    const auto& values = var->getVec();
    
    switch (uniformType) {
        case GL_FLOAT_VEC2:
            if (values.size() >= 2) {
                shader->setUniform2f(name, (float)values[0], (float)values[1]);
            }
            else if (values.size() == 1) {
                shader->setUniform2f(name, (float)values[0], (float)values[0]);
            }
            break;
        case GL_FLOAT_VEC3:
            if (values.size() >= 3) {
                shader->setUniform3f(name, (float)values[0], (float)values[1], (float)values[2]);
            }
            else if (values.size() == 2) {
                shader->setUniform3f(name, (float)values[0], (float)values[1], 0.0f);
            }
            else if (values.size() == 1) {
                shader->setUniform3f(name, (float)values[0], (float)values[0], (float)values[0]);
            }
            break;
        case GL_FLOAT_VEC4:
            if (values.size() >= 4) {
                shader->setUniform4f(name, (float)values[0], (float)values[1], (float)values[2], (float)values[3]);
            }
            else if (values.size() == 3) {
                shader->setUniform4f(name, (float)values[0], (float)values[1], (float)values[2], 0.0f);
            }
            else if (values.size() == 2) {
                shader->setUniform4f(name, (float)values[0], (float)values[1], 0.0f, 0.0f);
            }
            else if (values.size() == 1) {
                shader->setUniform4f(name, (float)values[0], (float)values[0], (float)values[0], (float)values[0]);
            }
            break;
        default:
            break;
    }
}

template<typename T>
void Shader::setUniforms(shared_ptr<T>& shader, const map<string, shared_ptr<BaseVar>>& vars) {
    const auto& types = getUniforms();

    // Built-in OpenFrameworks uniforms that are set automatically
    static const std::set<string> builtInUniforms = {
        "projectionMatrix",
        "modelViewMatrix",
        "modelViewProjectionMatrix",
        "textureMatrix",
    };
    
    // Loop through uniform types to ensure all shader uniforms are set
    for (const auto& typeIt : types) {
        const string& name = typeIt.first;
        GLenum uniformType = typeIt.second.type;

        // Check if it's a type we can handle in the switch statement
        bool isHandledType = (uniformType == GL_INT || uniformType == GL_INT_VEC2 || uniformType == GL_INT_VEC3 || uniformType == GL_INT_VEC4 ||
                              uniformType == GL_FLOAT || uniformType == GL_FLOAT_VEC2 || uniformType == GL_FLOAT_VEC3 ||
                              uniformType == GL_FLOAT_VEC4 || uniformType == GL_FLOAT_MAT4);
        if (!isHandledType) {
            continue;
        }
        
        // Find corresponding variable in vars
        auto varIt = vars.find(name);
        if (varIt == vars.end()) {
            continue;
        }
        
        switch (uniformType) {
            case GL_INT:
            case GL_INT_VEC2:
            case GL_INT_VEC3:
            case GL_INT_VEC4: {
                const auto intVar = std::dynamic_pointer_cast<const Variable<int>>(varIt->second);
                if (intVar != NULL) {
                    setUniformFromIntVar(shader, name, intVar.get(), uniformType);
                }
                else {
                    const auto floatVar = std::dynamic_pointer_cast<const Variable<float>>(varIt->second);
                    if (floatVar != NULL && floatVar->getVec().size() >= 1) {
                        shader->setUniform1i(name, (int)floatVar->getVec()[0]);
                    }
                    else {
                        // todo: log error
                    }
                }
                break;
            }
            case GL_FLOAT_VEC2: {
                if (!trySetUniformVec2(shader, name, varIt->second)) {
                    const auto floatVar = std::dynamic_pointer_cast<const Variable<float>>(varIt->second);
                    if (floatVar != NULL) {
                        setUniformFromFloatVar(shader, name, floatVar.get(), uniformType);
                    }
                    else {
                        const auto intVar = std::dynamic_pointer_cast<const Variable<int>>(varIt->second);
                        if (intVar != NULL) {
                            setUniformFromFloatFromInt(shader, name, intVar.get(), uniformType);
                        }
                    }
                }
                break;
            }
            case GL_FLOAT_VEC3: {
                if (!trySetUniformVec3(shader, name, varIt->second)) {
                    const auto floatVar = std::dynamic_pointer_cast<const Variable<float>>(varIt->second);
                    if (floatVar != NULL) {
                        setUniformFromFloatVar(shader, name, floatVar.get(), uniformType);
                    }
                    else {
                        const auto intVar = std::dynamic_pointer_cast<const Variable<int>>(varIt->second);
                        if (intVar != NULL) {
                            setUniformFromFloatFromInt(shader, name, intVar.get(), uniformType);
                        }
                    }
                }
                break;
            }
            case GL_FLOAT_VEC4: {
                if (!trySetUniformVec4(shader, name, varIt->second)) {
                    const auto floatVar = std::dynamic_pointer_cast<const Variable<float>>(varIt->second);
                    if (floatVar != NULL) {
                        setUniformFromFloatVar(shader, name, floatVar.get(), uniformType);
                    }
                    else {
                        const auto intVar = std::dynamic_pointer_cast<const Variable<int>>(varIt->second);
                        if (intVar != NULL) {
                            setUniformFromFloatFromInt(shader, name, intVar.get(), uniformType);
                        }
                    }
                }
                break;
            }
            case GL_FLOAT_MAT4: {
                const auto mat4Var = std::dynamic_pointer_cast<const Variable<glm::mat4>>(varIt->second);
                if (mat4Var != NULL) {
                    shader->setUniformMatrix4f(name, mat4Var->get());
                }
                break;
            }
            case GL_FLOAT:
            default: {
                const auto floatVar = std::dynamic_pointer_cast<const Variable<float>>(varIt->second);
                if (floatVar != NULL) {
                    setUniformFromFloatVar(shader, name, floatVar.get(), uniformType);
                }
                else {
                    // Try other types as fallback
                    const auto intVar = std::dynamic_pointer_cast<const Variable<int>>(varIt->second);
                    if (intVar != NULL && intVar->getVec().size() >= 1) {
                        shader->setUniform1f(name, (float)intVar->getVec()[0]);
                    }
                    else {
                        // Try glm vectors as fallback
                        if (trySetUniformVec2(shader, name, varIt->second) ||
                            trySetUniformVec3(shader, name, varIt->second) ||
                            trySetUniformVec4(shader, name, varIt->second)) {
                            // Successfully set from glm vector
                        }
                    }
                }
                break;
            }
        }
    }
}

template<typename T>
void Shader::setLights(shared_ptr<T>& shader) {
    vector<glm::vec3> lightPos;
    const map<string, shared_ptr<Light>>& lights = Lights::get().all();
    for (map<string, shared_ptr<Light>>::const_iterator it=lights.begin(); it!=lights.end(); ++it) {
        lightPos.push_back(it->second->getVarVec3("pos"));
    }
    int numLights = MIN(MAX_LIGHTS, lightPos.size());
    if (numLights > 0) {
        shader->setUniform3fv("lights", &lightPos[0][0], numLights);
    }
    shader->setUniform1i("numLights", numLights);
}

template<typename T>
void Shader::setUniformCameraMatrices(shared_ptr<T>& shader, const ofCamera& cam, const string& prefix) {
    shader->setUniformMatrix4f(prefix + "ModelViewMatrix", cam.getModelViewMatrix() );
    shader->setUniformMatrix4f(prefix + "ProjectionMatrix", cam.getProjectionMatrix() );
    shader->setUniformMatrix4f(prefix + "ModelViewProjectionMatrix", cam.getModelViewProjectionMatrix() );
}

void Shader::setUniformMaterial(ofMaterial& mat, const string& prefix) {
    if (shadertoy == NULL) {
        setUniformMaterial(shader, mat, prefix);
    }
    else {
        setUniformMaterial(shadertoy, mat, prefix);
    }
}

template<typename T>
void Shader::setUniformMaterial(shared_ptr<T>& shader, ofMaterial& mat, const string& prefix) {
    shader->setUniform4f(prefix + "Emission", mat.getEmissiveColor());
    shader->setUniform4f(prefix + "Ambient", mat.getAmbientColor());
    shader->setUniform4f(prefix + "Diffuse", mat.getDiffuseColor());
    shader->setUniform4f(prefix + "Specular", mat.getSpecularColor());
    shader->setUniform1f(prefix + "Shininess", mat.getShininess());
}

void Shader::reset() {
    unload();
    TexturePool::clean(_id);
    textures.clear();
    buffers.clear();
    VariablePool::cleanup(_id);
    vars.clear();
}

void Shader::unload() {
    shaderPath = "";
    directSource.clear();
    fileWatch.reset();
    if (shader != NULL) {
        if (shader->isLoaded()) {
            shader->unload();
        }
        shader.reset();
    }
    shadertoy.reset();
    clearUniforms();
}

bool Shader::getExportSource(string& source, string& reason) const {
    if (!isShaderLoaded() || directSource.empty()) {
        reason = "loaded shader source provenance is unavailable";
        return false;
    }
    const string active = shader->getShaderSource(GL_FRAGMENT_SHADER);
    if (active.empty() || active.size() > 128 * 1024) {
        reason = "active fragment source is empty or exceeds the recipe bound";
        return false;
    }
    // Query the program object rather than rereading the watched path. This is
    // the exact source retained by the most recent successful compile and is
    // truthful even across file-watch reloads and A->B->A disk changes.
    source = active;
    return true;
}

shared_ptr<Texture> Shader::getDefaultTexture() const {
    const auto texture = textures.find(DEFAULT_TEX);
    return texture == textures.end() ? nullptr : texture->second;
}

void Shader::setDefaultTexture(shared_ptr<Texture>& tex) {
    textures[DEFAULT_TEX] = tex;
}

shared_ptr<Texture> Shader::loadSceneTexture(const string& name, const string& path, bool feedback,
                                             const glm::vec2& defaultSize) {
    auto texture = TexturePool::getOrCreate(name, this);
    textures[name] = texture;
    const auto size = texture->data.getSize();
    if (size.x <= 0 || size.y <= 0)
        texture->data.setSize(size.x > 0 ? size.x : defaultSize.x, size.y > 0 ? size.y : defaultSize.y);
    texture->loadSceneAsset(path, feedback);
    return texture;
}

void Shader::setTexture(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    setTexture(name, m, 2);
}

void Shader::setTexture(const string& name, const ofxOscMessage& m, int arg) {
    string source = m.getArgAsString(arg);
    if (TexturePool::hasShared(source)) {
        textures[name] = TexturePool::getShared(source);
    }
    else {
        textures[name] = TexturePool::getOrCreate(name, this);
        textures[name]->load(m, arg);
    }
    const auto size = textures[name]->data.getSize();
    if (size.x <= 0 || size.y <= 0) {
        textures[name]->data.setSize(size.x > 0 ? size.x : std::max(1, ofGetWidth()),
                                     size.y > 0 ? size.y : std::max(1, ofGetHeight()));
    }
}

void Shader::setBuffer(const ofxOscMessage& m) {
    string name = m.getArgAsString(1);
    setBuffer(name, m, 2);
}

void Shader::setBuffer(const string& name, const ofxOscMessage& m, int arg) {
    if (buffers.find(name) != buffers.end()) {
        buffers.erase(name);
    }
    buffers[name] = make_shared<Buffer>(name, m, arg, *this);
}

void Shader::setUniform1i(const string& name, int v1) {
    if (shadertoy == NULL) {
        shader->setUniform1i(name, v1);
    }
    else {
        shadertoy->setUniform1i(name, v1);
    }
}

void Shader::setUniform2f(const string& name, float v1, float v2) {
    if (shadertoy == NULL) {
        shader->setUniform2f(name, v1, v2);
    }
    else {
        shadertoy->setUniform2f(name, v1, v2);
    }
}

void Shader::setUniformTexture(const string& name, ofTexture& tex, int loc) {
    loc = loc >= 0 ? loc : getNextTextureLocation();
    if (shadertoy == NULL) {
        // Check if texture is GL_TEXTURE_2D_ARRAY
        GLenum textureTarget = tex.getTextureData().textureTarget;
        if (textureTarget == GL_TEXTURE_2D_ARRAY) {
            shader->setUniformTexture(name, GL_TEXTURE_2D_ARRAY, (GLint)tex.getTextureData().textureID, loc);
        }
        else {
            shader->setUniformTexture(name, tex, loc);
        }
    }
    else {
        shadertoy->setUniformTexture(name, tex, loc);
    }
}

void Shader::set(const ofxOscMessage& m) {
    if (isShaderLoaded()) {
        string prop = m.getArgAsString(1);
        if (prop == "geomInputType") {
            const auto value = static_cast<GLenum>(m.getArgAsInt(2));
            if (fileWatch) fileWatch->geometryInput = value;
            shader->setGeometryInputType(value);
        }
        else if (prop == "geomOutputType") {
            const auto value = static_cast<GLenum>(m.getArgAsInt(2));
            if (fileWatch) fileWatch->geometryOutput = value;
            shader->setGeometryOutputType(value);
        }
        else if (prop == "geomOutputCount") {
            const auto value = m.getArgAsInt(2);
            if (fileWatch) fileWatch->geometryCount = value;
            shader->setGeometryOutputCount(value);
        }
    }
}

void Shader::clearUniforms() {
    uniforms.clear();
    uniformProgram = 0;
}

const Shader::Uniforms& Shader::getUniforms() const {
    const GLuint program = isShaderLoaded() ? shader->getProgram() : 0;
    if (uniformProgram != program) {
        uniforms = reflectUniforms(program);
        uniformProgram = program;
    }
    return uniforms;
}

map<string, string> Shader::getUniformValues() const {
    map<string, string> values;
    
    if (!isShaderLoaded() || shader == NULL) {
        return values;
    }
    
    GLuint program = shader->getProgram();
    if (program == 0) {
        return values;
    }
    
    const auto& reflected = getUniforms();
    
    for (const auto& it : reflected) {
        const string& name = it.first;
        GLenum type = it.second.type;
        
        GLint location = glGetUniformLocation(program, name.c_str());
        if (location < 0) {
            continue;
        }
        
        string valueStr;
        
        // Query uniform value based on type
        if (type == GL_FLOAT) {
            float value;
            glGetUniformfv(program, location, &value);
            valueStr = ofToString(value);
        }
        else if (type == GL_INT || type == GL_SAMPLER_2D || type == GL_SAMPLER_3D || 
                 type == GL_SAMPLER_CUBE || type == GL_SAMPLER_BUFFER) {
            int value;
            glGetUniformiv(program, location, &value);
            valueStr = ofToString(value);
        }
        else if (type == GL_FLOAT_VEC2) {
            float values[2];
            glGetUniformfv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + "]";
        }
        else if (type == GL_FLOAT_VEC3) {
            float values[3];
            glGetUniformfv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + ", " + ofToString(values[2]) + "]";
        }
        else if (type == GL_FLOAT_VEC4) {
            float values[4];
            glGetUniformfv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + ", " + ofToString(values[2]) + ", " + ofToString(values[3]) + "]";
        }
        else if (type == GL_INT_VEC2) {
            int values[2];
            glGetUniformiv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + "]";
        }
        else if (type == GL_INT_VEC3) {
            int values[3];
            glGetUniformiv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + ", " + ofToString(values[2]) + "]";
        }
        else if (type == GL_INT_VEC4) {
            int values[4];
            glGetUniformiv(program, location, values);
            valueStr = "[" + ofToString(values[0]) + ", " + ofToString(values[1]) + ", " + ofToString(values[2]) + ", " + ofToString(values[3]) + "]";
        }
        
        if (!valueStr.empty()) {
            values[name] = valueStr;
        }
    }
    
    return values;
}

template void Shader::setUniformFloatArray(shared_ptr<ofShader>& shader, const string& name, const vector<float>& values);
template void Shader::setUniformFloatArray(shared_ptr<ofxShadertoy>& shader, const string& name, const vector<float>& values);
template void Shader::setUniformIntArray(shared_ptr<ofShader>& shader, const string& name, const vector<int>& values);
template void Shader::setUniformIntArray(shared_ptr<ofxShadertoy>& shader, const string& name, const vector<int>& values);
template void Shader::setUniformFromFloatVar(shared_ptr<ofShader>& shader, const string& name, const Variable<float>* var, GLenum uniformType);
template void Shader::setUniformFromFloatVar(shared_ptr<ofxShadertoy>& shader, const string& name, const Variable<float>* var, GLenum uniformType);
template void Shader::setUniformFromIntVar(shared_ptr<ofShader>& shader, const string& name, const Variable<int>* var, GLenum uniformType);
template void Shader::setUniformFromIntVar(shared_ptr<ofxShadertoy>& shader, const string& name, const Variable<int>* var, GLenum uniformType);
template void Shader::setUniformFromFloatFromInt(shared_ptr<ofShader>& shader, const string& name, const Variable<int>* var, GLenum uniformType);
template void Shader::setUniformFromFloatFromInt(shared_ptr<ofxShadertoy>& shader, const string& name, const Variable<int>* var, GLenum uniformType);
template void Shader::setUniforms(shared_ptr<ofShader>& shader, const map<string, shared_ptr<BaseVar>>& vars);
template void Shader::setUniforms(shared_ptr<ofxShadertoy>& shader, const map<string, shared_ptr<BaseVar>>& vars);
template void Shader::setLights(shared_ptr<ofShader>& shader);
template void Shader::setLights(shared_ptr<ofxShadertoy>& shader);
