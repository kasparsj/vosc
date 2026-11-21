#include "var/Variable.h"
#include "var/ComponentLiteral.hpp"
#include "layer/Layer.h"
#include "ResourceRegistry.hpp"
#include "var/VariablePool.h"
#include <clocale>
#include <iostream>
#include <stdexcept>

static void require(bool condition) {
    if (!condition) throw std::runtime_error("variable runtime regression");
}

template<typename T>
static void repeated(const std::vector<std::string>& expressions) {
    Variable<T> variable;
    ofxOscMessage message;
    message.addStringArg("[\"i/total\",\"(i+1)/total\"" +
        std::string(expressions.size() >= 3 ? ",\"total+i\"" : "") +
        std::string(expressions.size() == 4 ? ",\"1\"" : "") + "]");
    message.addIntArg(2);
    variable.set(message, 0);
    for (int frame = 0; frame < 2; ++frame) {
        variable.update();
        require(variable.size() == 2);
        for (int i = 0; i < 2; ++i) {
            const auto& value = variable.get(i);
            require(value[0] == i / 2.f && value[1] == (i + 1) / 2.f);
            if (expressions.size() >= 3) require(value[2] == 2.f + i);
            if (expressions.size() == 4) require(value[3] == 1.f);
        }
    }
}

static void positionBinding() {
    ResourceRegistry registry;
    registry.activate();
    Layer layer;
    auto offset = VariablePool::createOrUpdateShared<float>("posOffset", 3.f);
    auto sendArray = [&layer](const string& payload) {
        ofxOscMessage message;
        message.addIntArg(0);
        message.addStringArg(payload);
        layer.layerCommand("/layer/pos", message);
    };
    const float maximum = std::numeric_limits<float>::max();
    sendArray("[3.4028234663852886e38,\"posOffset\",-3.4028234663852886e38]");
    VariablePool::update({});
    require(layer.getVarVec3("pos") == glm::vec3(maximum, 3.f, -maximum));
    sendArray("[1.401298464324817e-45,\"posOffset+i+total-1\",0]");
    VariablePool::update({});
    const auto previous = layer.vars.at("pos");
    auto& pool = VariablePool::getPool(&layer);
    require(pool.at("pos") == previous);
    require(layer.getVarVec3("pos").x == std::numeric_limits<float>::denorm_min());

    for (const auto* payload : {R"([8,"1+",0])", R"([8,"2\u0000+",0])",
                                R"([8,"posOffest",0])"}) {
        sendArray(payload);
        require(layer.vars.at("pos") == previous && pool.at("pos") == previous);
    }

    // muParser rejects expressions at its maximum source length without throwing through ofxExpr.
    sendArray("[8,\"" + string(20000, '1') + "\",0]");
    require(layer.vars.at("pos") == previous && pool.at("pos") == previous);

    // The parser rejects an invalid variable identifier during dependency registration.
    registry.variableSharedPool.emplace("bad name", offset);
    bool failed = false;
    try {
        layer.setPosition({8.f, string("posOffset"), 0.f});
    } catch (...) {
        failed = true;
    }
    registry.variableSharedPool.erase("bad name");
    require(failed);
    require(layer.vars.at("pos") == previous && pool.at("pos") == previous);
    offset->set(7.f);
    VariablePool::update({});
    require(layer.getVarVec3("pos").y == 7.f);

    // Validation must not execute assignments in an otherwise valid expression.
    sendArray("[8,\"posOffset=19\",0]");
    require(layer.vars.at("pos") != previous);
    require(offset->get() == 7.f);
    VariablePool::update({});
    require(offset->get() == 19.f && layer.getVarVec3("pos").y == 19.f);
}

static void integerUniformBinding() {
    ResourceRegistry registry;
    registry.activate();
    Shader shader;
    const auto shared = VariablePool::createOrUpdateShared<float>("fixtureOffset", -1.75f);
    constexpr auto maximum = std::numeric_limits<std::int32_t>::max();
    constexpr auto minimum = std::numeric_limits<std::int32_t>::min();
    shader.setIntegerVector("probe", 4, {16777217, maximum, minimum, string("fixtureOffset")});
    const auto previous = shader.vars.at("probe");
    auto& pool = VariablePool::getPool(&shader);
    require(pool.at("probe") == previous);
    const auto value = std::dynamic_pointer_cast<Variable<int>>(previous);
    require(bool(value));
    VariablePool::update({});
    require(value->getVec() == vector<int>({16777217, maximum, minimum, -1}));

    for (const string expression : {"1+", "missingFixtureOffset"}) {
        bool failed = false;
        try {
            shader.setIntegerVector("probe", 4, {7, 8, 9, expression});
        } catch (const std::invalid_argument&) {
            failed = true;
        }
        require(failed && shader.vars.at("probe") == previous && pool.at("probe") == previous);
    }

    shared->set(2147483648.f);
    VariablePool::update({});
    require(value->getVec() == vector<int>({16777217, maximum, minimum, -1}));

    shader.setIntegerVector("probe", 2, {16777217, string("fixtureOffset=19")});
    require(shared->get() == 2147483648.f);
    VariablePool::update({});
    const auto assigned = std::dynamic_pointer_cast<Variable<int>>(shader.vars.at("probe"));
    require(bool(assigned) && assigned->getVec() == vector<int>({16777217, 19}));
    require(shared->get() == 19.f);
}

static void floatUniformBinding() {
    ResourceRegistry registry;
    registry.activate();
    Shader shader;
    const auto shared = VariablePool::createOrUpdateShared<float>("fixtureOffset", 3.f);
    auto& pool = VariablePool::getPool(&shader);
    const float minimum = std::numeric_limits<float>::denorm_min();
    const float maximum = std::numeric_limits<float>::max();
    shader.setFloatVector("probe", 4, {minimum, maximum, -minimum, string("fixtureOffset+i+total")});
    const auto previous = shader.vars.at("probe");
    require(pool.at("probe") == previous);
    const auto color = std::dynamic_pointer_cast<Variable<ofFloatColor>>(previous);
    require(bool(color));
    VariablePool::update({});
    require(color->get().r == minimum && color->get().g == maximum &&
            color->get().b == -minimum && color->get().a == 4.f);

    auto reject = [&](const string& name, const vector<Shader::FloatComponent>& components) {
        bool failed = false;
        try {
            shader.setFloatVector(name, 4, components);
        } catch (const std::invalid_argument&) {
            failed = true;
        }
        require(failed);
        require(shader.vars.at("probe") == previous && pool.at("probe") == previous);
    };
    reject("probe", {1.f, 2.f, 3.f});
    reject("probe", {1.f, std::numeric_limits<float>::infinity(), 3.f, 4.f});
    for (const string& expression : {string("1+"), string("1\0+2", 4), string("missingOffset")})
        reject("probe", {8.f, 9.f, expression, 10.f});
    reject("newProbe", {8.f, 9.f, string("1+"), 10.f});
    require(shader.vars.count("newProbe") == 0 && pool.count("newProbe") == 0);

    registry.variableSharedPool.emplace("bad name", shared);
    reject("probe", {8.f, 9.f, string("fixtureOffset"), 10.f});
    registry.variableSharedPool.erase("bad name");

    shared->set(7.f);
    VariablePool::update({});
    require(color->get().a == 8.f);
    shader.setFloatVector("probe", 2, {1.f, 2.f});
    const auto pair = std::dynamic_pointer_cast<Variable<glm::vec2>>(shader.vars.at("probe"));
    require(bool(pair) && pool.at("probe") == pair && pair->get() == glm::vec2(1.f, 2.f));
}

static void mixedFloatLocale() {
    struct RestoreNumericLocale {
        const string previous = std::setlocale(LC_NUMERIC, nullptr);
        ~RestoreNumericLocale() { std::setlocale(LC_NUMERIC, previous.c_str()); }
    } restore;
    require(std::setlocale(LC_NUMERIC, "de_DE.UTF-8") != nullptr);
    require(string(std::localeconv()->decimal_point) == ",");

    ResourceRegistry registry;
    registry.activate();
    Shader shader;
    const float minimum = std::numeric_limits<float>::denorm_min();
    shader.setFloatVector("pair", 2, {minimum, string("1+2")});
    shader.setFloatVector("color", 4, {0.25f, -minimum, 1.5f, string("1+2")});
    VariablePool::update({});
    const auto pair = std::dynamic_pointer_cast<Variable<glm::vec2>>(shader.vars.at("pair"));
    const auto color = std::dynamic_pointer_cast<Variable<ofFloatColor>>(shader.vars.at("color"));
    require(bool(pair) && pair->get() == glm::vec2(minimum, 3.f));
    require(bool(color) && color->get().r == 0.25f && color->get().g == -minimum &&
            color->get().b == 1.5f && color->get().a == 3.f);

    // The recipe exporter reads the same stored literal texts.
    const auto positive = parseComponentLiteral(pair->getExpressions().at(0));
    const auto fraction = parseComponentLiteral(color->getExpressions().at(0));
    const auto negative = parseComponentLiteral(color->getExpressions().at(1));
    require(positive && static_cast<float>(*positive) == minimum);
    require(fraction && *fraction == 0.25);
    require(negative && static_cast<float>(*negative) == -minimum);
    for (const auto* malformed : {"0,25", "0.25junk", "nan", "1e400", ""})
        require(!parseComponentLiteral(malformed));
    require(!parseComponentLiteral(string("0.25\0junk", 9)));
}

int main() {
    repeated<glm::vec2>({"i/total", "(i+1)/total"});
    repeated<glm::vec3>({"i/total", "(i+1)/total", "total+i"});
    repeated<ofFloatColor>({"i/total", "(i+1)/total", "total+i", "1"});
    Variable<glm::vec2> mixed;
    mixed.set({"1.40129846e-45", "1/total"}, {false, true});
    mixed.update();
    require(mixed.get().x == std::numeric_limits<float>::denorm_min());
    require(mixed.get().y == 1.f);
    positionBinding();
    integerUniformBinding();
    floatUniformBinding();
    mixedFloatLocale();
    std::cout << "PASS repeated vectors, position and shader-uniform precision, locale independence, binding rollback\n";
}
