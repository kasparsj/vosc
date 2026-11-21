// Diagnostic-only DYLD interposer. Never use its timings for acceptance.
#include <OpenGL/gl3.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

namespace {
std::atomic<unsigned> sequence{0};
int traceFd() {
    static int fd = [] { const char* path = std::getenv("VOSC_GL_TRACE");
        return path && path[0] == '/' ? open(path, O_WRONLY|O_CREAT|O_EXCL, 0600) : -1; }();
    return fd;
}
void entry(const char* event, unsigned a=0, unsigned b=0, unsigned c=0) {
    unsigned n=sequence.fetch_add(1); if (n >= 65536 || traceFd()<0) return;
    dprintf(traceFd(), "{\"seq\":%u,\"event\":\"%s\",\"a\":%u,\"b\":%u,\"c\":%u}\n",n,event,a,b,c);
}
void draw(const char* event, GLenum mode, GLsizei count, GLenum type=0) {
    GLint program=0,vao=0,fbo=0;
    glGetIntegerv(GL_CURRENT_PROGRAM,&program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&vao);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&fbo);
    entry(event,mode,count,type);entry("draw_state",program,vao,fbo);entry("draw_depth",glIsEnabled(GL_DEPTH_TEST));
    // Diagnostic readback establishes actual position bytes, not just VAO IDs.
    GLint buffer=0,size=0,old=0; glGetVertexAttribiv(0,GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING,&buffer);
    if (buffer) {
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&old);glBindBuffer(GL_ARRAY_BUFFER,buffer);
        glGetBufferParameteriv(GL_ARRAY_BUFFER,GL_BUFFER_SIZE,&size);
        if(size>0 && size<=65536) {
            unsigned char bytes[65536];glGetBufferSubData(GL_ARRAY_BUFFER,0,size,bytes);
            unsigned hash=2166136261u;for(int i=0;i<size;++i)hash=(hash^bytes[i])*16777619u;
            entry("position_bytes",size,hash,buffer);
        }
        glBindBuffer(GL_ARRAY_BUFFER,old);
    }
}
void gen(GLsizei n,GLuint* ids){glGenQueries(n,ids);for(int i=0;i<n;++i)entry("query_alloc",ids[i],n);}
void del(GLsizei n,const GLuint* ids){for(int i=0;i<n;++i)entry("query_delete",ids[i],n);glDeleteQueries(n,ids);}
void begin(GLenum target,GLuint id){GLint active=0;glGetQueryiv(target,GL_CURRENT_QUERY,&active);entry("query_begin",id,target,active);glBeginQuery(target,id);}
void end(GLenum target){GLint active=0;glGetQueryiv(target,GL_CURRENT_QUERY,&active);entry("query_end",active,target);glEndQuery(target);}
void available(GLuint id,GLenum name,GLint* value){glGetQueryObjectiv(id,name,value);entry("query_available",id,name,*value);}
void result(GLuint id,GLenum name,GLuint64* value){glGetQueryObjectui64v(id,name,value);entry("query_result",id,name);}
void arrays(GLenum mode,GLint first,GLsizei count){draw("draw_arrays",mode,count);glDrawArrays(mode,first,count);}
void elements(GLenum mode,GLsizei count,GLenum type,const void* indices){draw("draw_elements",mode,count,type);glDrawElements(mode,count,type,indices);}
void finish(){entry("finish");glFinish();}
}
#define INTERPOSE(replacement, original) \
__attribute__((used)) static struct { const void* replacement; const void* original; } interpose_##original \
__attribute__((section("__DATA,__interpose"))) = { (const void*)&replacement, (const void*)&original }
INTERPOSE(gen,glGenQueries);INTERPOSE(del,glDeleteQueries);INTERPOSE(begin,glBeginQuery);
INTERPOSE(end,glEndQuery);INTERPOSE(available,glGetQueryObjectiv);INTERPOSE(result,glGetQueryObjectui64v);
INTERPOSE(arrays,glDrawArrays);INTERPOSE(elements,glDrawElements);INTERPOSE(finish,glFinish);
