// Shader dump for the strict shader check (tools/check_shaders_strict.sh): preloaded
// (LD_PRELOAD) into a host GL check, it writes every shader source the app hands to
// glShaderSource into $RE4_SHADER_DUMP as NNNN.vert / NNNN.frag / NNNN.comp, then
// passes the call on. Mesa accepts some GLSL that stricter drivers (Mali) refuse;
// glslangValidator reads the files afterwards.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef char GLchar;
typedef unsigned int GLenum;

#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPUTE_SHADER 0x91B9
#define GL_SHADER_TYPE 0x8B4F

static int g_count;

void glShaderSource(GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)
{
    static void (*real)(GLuint, GLsizei, const GLchar* const*, const GLint*);
    static void (*getShaderiv)(GLuint, GLenum, GLint*);
    if (!real) {
        real = (void (*)(GLuint, GLsizei, const GLchar* const*, const GLint*)) dlsym(RTLD_NEXT, "glShaderSource");
        getShaderiv = (void (*)(GLuint, GLenum, GLint*)) dlsym(RTLD_NEXT, "glGetShaderiv");
    }
    const char* dir = getenv("RE4_SHADER_DUMP");
    if (dir && *dir && getShaderiv) {
        GLint type = 0;
        getShaderiv(shader, GL_SHADER_TYPE, &type);
        const char* ext = type == GL_VERTEX_SHADER ? "vert" : type == GL_COMPUTE_SHADER ? "comp" : "frag";
        char path[4096];
        snprintf(path, sizeof path, "%s/%05d.%s", dir, g_count++, ext);
        FILE* f = fopen(path, "w");
        if (f) {
            for (GLsizei i = 0; i < count; i++) {
                if (!string[i]) {
                    continue;
                }
                size_t n = length && length[i] >= 0 ? (size_t) length[i] : strlen(string[i]);
                fwrite(string[i], 1, n, f);
            }
            fclose(f);
        }
    }
    real(shader, count, string, length);
}
