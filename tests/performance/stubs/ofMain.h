#pragma once

#include <cstddef>
#include <cstdint>

using GLenum = unsigned int;
using GLint = int;
using GLsizei = int;
using GLuint = unsigned int;
using GLuint64 = std::uint64_t;

constexpr GLenum GL_NO_ERROR = 0;
constexpr GLenum GL_INVALID_OPERATION = 0x0502;
constexpr GLenum GL_TIME_ELAPSED = 0x88BF;
constexpr GLenum GL_QUERY_COUNTER_BITS = 0x8864;
constexpr GLenum GL_QUERY_RESULT = 0x8866;
constexpr GLenum GL_QUERY_RESULT_AVAILABLE = 0x8867;
constexpr GLint GL_FALSE = 0;
constexpr GLint GL_TRUE = 1;

GLenum glGetError();
void glGetQueryiv(GLenum target, GLenum name, GLint* value);
void glGenQueries(GLsizei count, GLuint* queries);
void glDeleteQueries(GLsizei count, const GLuint* queries);
void glBeginQuery(GLenum target, GLuint query);
void glEndQuery(GLenum target);
void glGetQueryObjectiv(GLuint query, GLenum name, GLint* value);
void glGetQueryObjectui64v(GLuint query, GLenum name, GLuint64* value);
