#include "control/PerformanceTelemetry.hpp"
#include "ofMain.h"

#include <array>
#include <cassert>
#include <deque>
#include <iostream>

namespace fakegl {

std::deque<GLenum> errors;
bool foreignActive = false;
bool ownedActive = false;
int generated = 0;
int deleted = 0;
int beginCalls = 0;
int endCalls = 0;
GLuint nextQuery = 1;

void reset() {
    errors.clear();
    foreignActive = false;
    ownedActive = false;
    generated = 0;
    deleted = 0;
    beginCalls = 0;
    endCalls = 0;
    nextQuery = 1;
}

} // namespace fakegl

GLenum glGetError() {
    if (fakegl::errors.empty()) return GL_NO_ERROR;
    const auto result = fakegl::errors.front();
    fakegl::errors.pop_front();
    return result;
}

void glGetQueryiv(GLenum target, GLenum name, GLint* value) {
    assert(target == GL_TIME_ELAPSED && name == GL_QUERY_COUNTER_BITS);
    *value = 64;
}

void glGenQueries(GLsizei count, GLuint* queries) {
    fakegl::generated += count;
    for (GLsizei index = 0; index < count; ++index) queries[index] = fakegl::nextQuery++;
}

void glDeleteQueries(GLsizei count, const GLuint*) { fakegl::deleted += count; }

void glBeginQuery(GLenum target, GLuint) {
    assert(target == GL_TIME_ELAPSED);
    ++fakegl::beginCalls;
    if (fakegl::foreignActive || fakegl::ownedActive) {
        fakegl::errors.push_back(GL_INVALID_OPERATION);
        return;
    }
    fakegl::ownedActive = true;
}

void glEndQuery(GLenum target) {
    assert(target == GL_TIME_ELAPSED);
    ++fakegl::endCalls;
    if (fakegl::ownedActive) fakegl::ownedActive = false;
    else if (fakegl::foreignActive) fakegl::foreignActive = false;
    else fakegl::errors.push_back(GL_INVALID_OPERATION);
}

void glGetQueryObjectiv(GLuint, GLenum name, GLint* value) {
    assert(name == GL_QUERY_RESULT_AVAILABLE);
    *value = GL_TRUE;
}

void glGetQueryObjectui64v(GLuint, GLenum name, GLuint64* value) {
    assert(name == GL_QUERY_RESULT);
    *value = 1'000'000;
}

namespace {

void testAmbientErrorDoesNotOrphanOwnedQuery() {
    fakegl::reset();
    auto backend = control::makeOpenGlQueryBackend();
    assert(backend->initialize());
    fakegl::errors.push_back(GL_INVALID_OPERATION);
    assert(backend->begin(0));
    assert(fakegl::ownedActive && fakegl::beginCalls == 1);
    backend->end();
    assert(!fakegl::ownedActive && fakegl::endCalls == 1);
    backend->shutdown();
    assert(fakegl::generated == 8 && fakegl::deleted == 8);
}

void testFailedBeginNeverEndsForeignQuery() {
    fakegl::reset();
    auto backend = control::makeOpenGlQueryBackend();
    assert(backend->initialize());
    fakegl::foreignActive = true;
    assert(!backend->begin(0));
    assert(!backend->supported());
    assert(fakegl::foreignActive);
    assert(fakegl::endCalls == 0);
    backend->end();
    backend->shutdown();
    assert(fakegl::foreignActive && fakegl::endCalls == 0);
    assert(fakegl::generated == 8 && fakegl::deleted == 8);
}

void testShutdownEndsOnlyOwnedQueryBeforeDeletingPool() {
    fakegl::reset();
    auto backend = control::makeOpenGlQueryBackend();
    assert(backend->initialize());
    assert(fakegl::generated == 8);
    assert(backend->begin(7));
    backend->shutdown();
    assert(!fakegl::ownedActive && fakegl::endCalls == 1);
    assert(fakegl::deleted == 8);
}

} // namespace

int main() {
    testAmbientErrorDoesNotOrphanOwnedQuery();
    testFailedBeginNeverEndsForeignQuery();
    testShutdownEndsOnlyOwnedQueryBeforeDeletingPool();
    std::cout << "Performance OpenGL backend tests passed\n";
}
