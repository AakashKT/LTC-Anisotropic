#include <cstdlib>
#include <cstdio>
#include <vector>

#include "glad/glad.h"
#include "GLFW/glfw3.h"
#include "imgui.h"
#include "imgui_impl.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define DJ_OPENGL_IMPLEMENTATION
#include "dj_opengl.h"

////////////////////////////////////////////////////////////////////////////////
// Tweakable Macros
//
////////////////////////////////////////////////////////////////////////////////
#define VIEWER_DEFAULT_WIDTH  (1920)
#define VIEWER_DEFAULT_HEIGHT (1080)

#ifndef PATH_TO_SRC_DIRECTORY
#   define PATH_TO_SRC_DIRECTORY "./"
#endif

#define PATH_TO_SHADER_DIRECTORY PATH_TO_SRC_DIRECTORY "shaders/"

#ifndef M_PI
#define M_PI 3.141592654
#endif

#define BUFFER_SIZE(x)    ((int)(sizeof(x)/sizeof(x[0])))

#define BUFFER_OFFSET(i) ((char *)NULL + (i))

#define LOG(fmt, ...) fprintf(stdout, fmt "\n", ##__VA_ARGS__); fflush(stdout);


////////////////////////////////////////////////////////////////////////////////
// Global Variables
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// Window Manager
enum { AA_NONE, AA_MSAA2, AA_MSAA4, AA_MSAA8, AA_MSAA16 };
struct Window {
    GLFWwindow* handle;
    const char *name;
    int32_t width, height;
    uint32_t frameID, maxFrameID;
    bool showHud;
    struct {
        int32_t major, minor;
    } glversion;
    struct {
        uint32_t isRunning, frameID, captureID;
    } recorder;
    struct {
        int32_t aa;
        struct { int32_t fixed; } msaa;
        struct { float r, g, b; } clearColor;
        uint32_t passID, samplesPerPixel, samplesPerPass;
        struct {bool progressive, reset;} flags;
    } framebuffer;
} g_window = {
    NULL,
    "LTC Aniso",
    VIEWER_DEFAULT_WIDTH, VIEWER_DEFAULT_HEIGHT,
    0u, ~0u,
    true,
    {4, 5},
    {0, 0, 0},
    {
        AA_NONE,
        {false},
        {61.0f / 255.0f, 119.0f / 255.0f, 192.0f / 255.0f},
        0u, 1u << 16u, 1u,
        {true, true}
    }
};


// -----------------------------------------------------------------------------
// Camera Manager
enum {
    TONEMAP_UNCHARTED2,
    TONEMAP_FILMIC,
    TONEMAP_ACES,
    TONEMAP_REINHARD,
    TONEMAP_RAW
};

#define INIT_POS glm::vec3(0.0f, 0.0f, 3.0f)
struct CameraManager {
    float fovy, zNear, zFar;    // perspective settings
    struct {
        int32_t tonemap;
    } sensor;                   // sensor (i.e., tone mapping technique)
    struct {
        float theta, phi;
    } angles;                   // axis
    float radius;               // 3D position
} g_camera = {
    55.f, 0.01f, 1024.f,
    {TONEMAP_RAW},
#if 0 // default
    {+0.32f, 0.87f},
    4.2f,
#else // buggy
    {+0.5f, 0.0f},
    2.25f
#endif
};
#undef INIT_POS


// -----------------------------------------------------------------------------
// Light Manager
struct LightManager {
    float intensity;
    float width, height;
    float yRot, zRot;
    float theta, phi;
    float distanceToOrigin;
} g_light = {
#if 0 // default
    3.0f,
    1.0f, 1.0f,
    0.0f, 0.0f,
    0.2f, 0.0f,
    1.9f
#else // buggy
    1.0f,
    15.0f, 15.0f,
    0.0f, 0.0f,
    0.0f, 0.0f,
    10.0f
#endif
};

glm::mat4 LightVertices()
{
    const glm::mat4 theta = glm::rotate(glm::mat4(1.0f), g_light.theta * 180.0f - 90.0f, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 phi = glm::rotate(glm::mat4(1.0f), g_light.phi * 360.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 yRot = glm::rotate(glm::mat4(1.0f), g_light.yRot * 360.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 zRot = glm::rotate(glm::mat4(1.0f), g_light.zRot * 360.0f, glm::vec3(0.0f, 0.0f, 1.0f));
    const glm::mat4 scale = glm::scale(glm::mat4(1.0f), glm::vec3(g_light.width, g_light.height, 1.0f));
    const glm::mat4 offset = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, g_light.distanceToOrigin));
    const glm::mat4 verticesStd = (glm::mat4(-0.5f, -0.5f, 0.0f, 1.0f,
                                             -0.5f, +0.5f, 0.0f, 1.0f,
                                             +0.5f, +0.5f, 0.0f, 1.0f,
                                             +0.5f, -0.5f, 0.0f, 1.0f));

    return phi * theta * offset * yRot * zRot * scale * verticesStd;
}

// -----------------------------------------------------------------------------
// Sphere Manager
enum {
    INTEGRATOR_GGX,
    INTEGRATOR_GGX_LIGHT,
    INTEGRATOR_GGX_MIS,
    INTEGRATOR_LTC,
    INTEGRATOR_LTC_ANALYTIC,
    INTEGRATOR_LTC_ANALYTIC_ISOTROPIC,
    INTEGRATOR_DEBUG
};
enum { ENTITY_SPHERE, ENTITY_QUAD, ENTITY_COUNT };
struct SphereManager {
    struct {bool showLines;} flags;
    struct {
        int xTess, yTess;
        int vertexCount, indexCount;
    } sphere;
    struct {
        int32_t integrator;
        bool l2;
        bool isotropic;
        glm::vec2 alpha;
    } ggx;
} g_sphere = {
    {false},
    {64, 128, -1, -1}, // sphere
    {
#if 0 // default
        INTEGRATOR_GGX_MIS,
        false,
        true,
        glm::vec2(0.4)
#else // buggy
    INTEGRATOR_LTC_ANALYTIC,
    false,
    false,
    glm::vec2(0.047, 0.109)
#endif
    }
};


// -----------------------------------------------------------------------------
// OpenGL resources
enum { FRAMEBUFFER_VIEWER, FRAMEBUFFER_SCENE, FRAMEBUFFER_COUNT };
enum { STREAM_TRANSFORM, STREAM_RANDOM, STREAM_COUNT };
enum { CLOCK_RENDER, CLOCK_COUNT };
enum { VERTEXARRAY_EMPTY, VERTEXARRAY_SPHERE, VERTEXARRAY_COUNT };
enum {
    BUFFER_SPHERE_INDEXES,
    BUFFER_SPHERE_VERTICES,
    BUFFER_LTC,
    BUFFER_LTC_ANISO,
    BUFFER_LTC_ANISO_L2,
    BUFFER_LTC_AMPLITUDE,

    BUFFER_COUNT
};
enum {
    TEXTURE_SCENE_COLOR_BUFFER,
    TEXTURE_SCENE_DEPTH_BUFFER,
    TEXTURE_VIEWER_COLOR_BUFFER,
    TEXTURE_LTC,
    TEXTURE_LTC_ANISO_1,
    TEXTURE_LTC_ANISO_2,
    TEXTURE_LTC_ANISO_3,

    TEXTURE_COUNT
};
enum {
    PROGRAM_VIEWER,
    PROGRAM_SPHERE,
    PROGRAM_LIGHT,
    PROGRAM_BACKGROUND,

    PROGRAM_COUNT
};
enum {
    UNIFORM_VIEWER_SCENE_FRAMEBUFFER_SAMPLER,
    UNIFORM_VIEWER_EXPOSURE,
    UNIFORM_VIEWER_GAMMA,
    UNIFORM_VIEWER_VIEWPORT,

    UNIFORM_SPHERE_SAMPLES_PER_PASS,
    UNIFORM_SPHERE_ALPHA,
    UNIFORM_SPHERE_LTC_SAMPLER,
    UNIFORM_SPHERE_LTC_ANISO_SAMPLER_1,
    UNIFORM_SPHERE_LTC_ANISO_SAMPLER_2,
    UNIFORM_SPHERE_LTC_ANISO_SAMPLER_3,
    UNIFORM_SPHERE_LIGHT_VERTICES,
    UNIFORM_SPHERE_LIGHT_INTENSITY,
    UNIFORM_SPHERE_LIGHT_AREA,

    UNIFORM_LIGHT_SAMPLES_PER_PASS,
    UNIFORM_LIGHT_LIGHT_VERTICES,
    UNIFORM_LIGHT_LIGHT_INTENSITY,
    UNIFORM_LIGHT_LIGHT_AREA,

    UNIFORM_BACKGROUND_SAMPLES_PER_PASS,

    UNIFORM_COUNT
};
struct OpenGLManager {
    GLuint programs[PROGRAM_COUNT];
    GLuint framebuffers[FRAMEBUFFER_COUNT];
    GLuint textures[TEXTURE_COUNT];
    GLuint vertexArrays[VERTEXARRAY_COUNT];
    GLuint buffers[BUFFER_COUNT];
    GLint uniforms[UNIFORM_COUNT];
    djg_buffer *streams[STREAM_COUNT];
    djg_clock *clocks[CLOCK_COUNT];
} g_gl = {{0}};


////////////////////////////////////////////////////////////////////////////////
// Utility functions
//
////////////////////////////////////////////////////////////////////////////////

float Radians(float degrees)
{
    return degrees * M_PI / 180.f;
}

glm::mat4 InverseViewMatrix()
{
#if 0
    const float c1 = cosf(g_camera.angles.up);
    const float s1 = sinf(g_camera.angles.up);
    const float c2 = cosf(g_camera.angles.side);
    const float s2 = sinf(g_camera.angles.side);
    //const glm::vec3 pos = g_camera.pos;

    return glm::transpose(glm::mat4(
        c1     , s1 * s2, c2 * s1, 0.0f,
        0.0f   ,      c2,     -s2, 0.0f,
        -s1    , c1 * s2, c1 * c2, 0.0f,
        0.0f   , 0.0f   , 0.0f   , 1.0f
    ));
#else
    const glm::mat4 theta = glm::rotate(glm::mat4(1.0f), g_camera.angles.theta * 180.0f - 90.0f, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 phi = glm::rotate(glm::mat4(1.0f), g_camera.angles.phi * 360.0f, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 radius = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, g_camera.radius));

    return phi * theta * radius;
#endif
}

static void APIENTRY
DebugOutputLogger(
    GLenum source,
    GLenum type,
    GLuint,
    GLenum severity,
    GLsizei,
    const GLchar* message,
    const GLvoid*
) {
    char srcstr[32], typestr[32];

    switch(source) {
        case GL_DEBUG_SOURCE_API: strcpy(srcstr, "OpenGL"); break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM: strcpy(srcstr, "Windows"); break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: strcpy(srcstr, "Shader Compiler"); break;
        case GL_DEBUG_SOURCE_THIRD_PARTY: strcpy(srcstr, "Third Party"); break;
        case GL_DEBUG_SOURCE_APPLICATION: strcpy(srcstr, "Application"); break;
        case GL_DEBUG_SOURCE_OTHER: strcpy(srcstr, "Other"); break;
        default: strcpy(srcstr, "???"); break;
    };

    switch(type) {
        case GL_DEBUG_TYPE_ERROR: strcpy(typestr, "Error"); break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: strcpy(typestr, "Deprecated Behavior"); break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR: strcpy(typestr, "Undefined Behavior"); break;
        case GL_DEBUG_TYPE_PORTABILITY: strcpy(typestr, "Portability"); break;
        case GL_DEBUG_TYPE_PERFORMANCE: strcpy(typestr, "Performance"); break;
        case GL_DEBUG_TYPE_OTHER: strcpy(typestr, "Message"); break;
        default: strcpy(typestr, "???"); break;
    }

    if(severity == GL_DEBUG_SEVERITY_HIGH) {
        LOG("djg_error: %s %s\n"                \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    } else if(severity == GL_DEBUG_SEVERITY_MEDIUM) {
        LOG("djg_warn: %s %s\n"                 \
                "-- Begin -- GL_debug_output\n" \
                "%s\n"                              \
                "-- End -- GL_debug_output\n",
                srcstr, typestr, message);
    }
}

void InitDebugOutput(void)
{
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(&DebugOutputLogger, NULL);
}


////////////////////////////////////////////////////////////////////////////////
// Program Configuration
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
// set viewer program uniforms
void ConfigureViewerProgram()
{
    glProgramUniform1i(g_gl.programs[PROGRAM_VIEWER],
                       g_gl.uniforms[UNIFORM_VIEWER_SCENE_FRAMEBUFFER_SAMPLER],
                       TEXTURE_SCENE_COLOR_BUFFER);
//    glProgramUniform1f(g_gl.programs[PROGRAM_VIEWER],
//                       g_gl.uniforms[UNIFORM_VIEWER_EXPOSURE],
//                       g_camera.exposure);
//    glProgramUniform1f(g_gl.programs[PROGRAM_VIEWER],
//                       g_gl.uniforms[UNIFORM_VIEWER_GAMMA],
//                       g_app.viewer.gamma);
}

// -----------------------------------------------------------------------------
// set Sphere program uniforms
void ConfigureSphereProgram()
{
    const glm::mat4 lightVertices = LightVertices();

    glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_SAMPLES_PER_PASS],
                       g_window.framebuffer.samplesPerPass);
    glProgramUniform2f(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_ALPHA],
                       g_sphere.ggx.alpha.x,
                       g_sphere.ggx.alpha.y);
    glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_LTC_SAMPLER],
                       TEXTURE_LTC);
    glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_1],
                       TEXTURE_LTC_ANISO_1);
    glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_2],
                       TEXTURE_LTC_ANISO_2);
    glProgramUniform1i(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_3],
                       TEXTURE_LTC_ANISO_3);
    glProgramUniformMatrix4fv(g_gl.programs[PROGRAM_SPHERE],
                              g_gl.uniforms[UNIFORM_SPHERE_LIGHT_VERTICES],
                              1, false, &lightVertices[0][0]);
    glProgramUniform1f(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_LIGHT_INTENSITY],
                       g_light.intensity);
    glProgramUniform1f(g_gl.programs[PROGRAM_SPHERE],
                       g_gl.uniforms[UNIFORM_SPHERE_LIGHT_AREA],
                       g_light.width * g_light.height);
//    LOG("%i %i %i",
//        g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_1],
//        g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_2],
//        g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_3]);
}

// -----------------------------------------------------------------------------
// set Light program uniforms
void ConfigureLightProgram()
{
    const glm::mat4 lightVertices = LightVertices();

    glProgramUniform1i(g_gl.programs[PROGRAM_LIGHT],
                       g_gl.uniforms[UNIFORM_LIGHT_SAMPLES_PER_PASS],
                       g_window.framebuffer.samplesPerPass);
    glProgramUniformMatrix4fv(g_gl.programs[PROGRAM_LIGHT],
                              g_gl.uniforms[UNIFORM_LIGHT_LIGHT_VERTICES],
                              1, false, &lightVertices[0][0]);
    glProgramUniform1f(g_gl.programs[PROGRAM_LIGHT],
                       g_gl.uniforms[UNIFORM_LIGHT_LIGHT_INTENSITY],
                       g_light.intensity);
}


// -----------------------------------------------------------------------------
// set Sphere program uniforms
void ConfigureBackgroundProgram()
{
    glProgramUniform1i(g_gl.programs[PROGRAM_BACKGROUND],
                       g_gl.uniforms[UNIFORM_BACKGROUND_SAMPLES_PER_PASS],
                       g_window.framebuffer.samplesPerPass);
}


////////////////////////////////////////////////////////////////////////////////
// Program Loading
//
////////////////////////////////////////////////////////////////////////////////


// -----------------------------------------------------------------------------
/**
 * Load the Viewer Program
 *
 * This program is responsible for blitting the scene framebuffer to
 * the back framebuffer, while applying gamma correction and tone mapping to
 * the rendering.
 */
bool LoadViewerProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_VIEWER];

    LOG("Loading {Framebuffer-Blit-Program}");
    if (g_window.framebuffer.aa >= AA_MSAA2 && g_window.framebuffer.aa <= AA_MSAA16)
        djgp_push_string(djp, "#define MSAA_FACTOR %i\n", 1 << g_window.framebuffer.aa);
    djgp_push_file(djp, PATH_TO_SHADER_DIRECTORY "Viewer.glsl");
    if (!djgp_to_gl(djp, 430, false, true, program)) {
        LOG("=> Failure <=");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_VIEWER_SCENE_FRAMEBUFFER_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_FramebufferSampler");
    g_gl.uniforms[UNIFORM_VIEWER_VIEWPORT] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Viewport");
    g_gl.uniforms[UNIFORM_VIEWER_EXPOSURE] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Exposure");
    g_gl.uniforms[UNIFORM_VIEWER_GAMMA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_VIEWER], "u_Gamma");

    ConfigureViewerProgram();

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load the Sphere Program
 *
 * This program is responsible for rendering the spheres to the
 * framebuffer
 */
bool LoadSphereProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_SPHERE];

    LOG("Loading {Sphere-Program}");
    switch (g_sphere.ggx.integrator) {
        case INTEGRATOR_DEBUG:
            djgp_push_string(djp, "#define INTEGRATOR_DEBUG 1\n");
            break;
        case INTEGRATOR_GGX:
            djgp_push_string(djp, "#define INTEGRATOR_GGX 1\n");
            break;
        case INTEGRATOR_GGX_LIGHT:
            djgp_push_string(djp, "#define INTEGRATOR_GGX_LIGHT 1\n");
            break;
        case INTEGRATOR_GGX_MIS:
            djgp_push_string(djp, "#define INTEGRATOR_GGX_MIS 1\n");
            break;
        case INTEGRATOR_LTC:
            djgp_push_string(djp, "#define INTEGRATOR_LTC 1\n");
            break;
        case INTEGRATOR_LTC_ANALYTIC:
            djgp_push_string(djp, "#define INTEGRATOR_LTC_ANALYTIC 1\n");
            break;
        case INTEGRATOR_LTC_ANALYTIC_ISOTROPIC:
            djgp_push_string(djp, "#define INTEGRATOR_LTC_ANALYTIC_ISOTROPIC 1\n");
            break;
    };

    djgp_push_string(djp, "#define BUFFER_BINDING_LTC %i\n", BUFFER_LTC);
    djgp_push_string(djp, "#define BUFFER_BINDING_LTC_ANISO %i\n", g_sphere.ggx.l2 ? BUFFER_LTC_ANISO_L2 : BUFFER_LTC_ANISO);
    djgp_push_string(djp, "#define BUFFER_BINDING_LTC_AMPLITUDE %i\n", BUFFER_LTC_AMPLITUDE);
    djgp_push_string(djp, "#define BUFFER_BINDING_RANDOM %i\n", STREAM_RANDOM);
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_file(djp, PATH_TO_SHADER_DIRECTORY "Sphere.glsl");

    if (!djgp_to_gl(djp, 430, false, true, program)) {
        LOG("=> Failure <=");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_SPHERE_LIGHT_VERTICES] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_LightVertices");
    g_gl.uniforms[UNIFORM_SPHERE_LIGHT_INTENSITY] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_LightIntensity");
    g_gl.uniforms[UNIFORM_SPHERE_LIGHT_AREA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_LightArea");
    g_gl.uniforms[UNIFORM_SPHERE_SAMPLES_PER_PASS] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_SamplesPerPass");
    g_gl.uniforms[UNIFORM_SPHERE_ALPHA] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_Alpha");
    g_gl.uniforms[UNIFORM_SPHERE_LTC_SAMPLER] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_LtcSampler");
    g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_1] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_LtcAnisoSampler1");
    g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_2] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_LtcAnisoSampler2");
    g_gl.uniforms[UNIFORM_SPHERE_LTC_ANISO_SAMPLER_3] =
        glGetUniformLocation(g_gl.programs[PROGRAM_SPHERE], "u_LtcAnisoSampler3");

    ConfigureSphereProgram();

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load the Light Program
 *
 * This program is responsible for rendering the area light.
 */
bool LoadLightProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_LIGHT];

    LOG("Loading {Light-Program}");
    djgp_push_string(djp, "#define BUFFER_BINDING_TRANSFORMS %i\n", STREAM_TRANSFORM);
    djgp_push_file(djp, PATH_TO_SHADER_DIRECTORY "Light.glsl");

    if (!djgp_to_gl(djp, 430, false, true, program)) {
        LOG("=> Failure <=");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_LIGHT_LIGHT_VERTICES] =
        glGetUniformLocation(g_gl.programs[PROGRAM_LIGHT], "u_LightVertices");
    g_gl.uniforms[UNIFORM_LIGHT_LIGHT_INTENSITY] =
        glGetUniformLocation(g_gl.programs[PROGRAM_LIGHT], "u_LightIntensity");
    g_gl.uniforms[UNIFORM_LIGHT_SAMPLES_PER_PASS] =
        glGetUniformLocation(g_gl.programs[PROGRAM_LIGHT], "u_SamplesPerPass");

    ConfigureLightProgram();

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 */
bool LoadBackgroundProgram()
{
    djg_program *djp = djgp_create();
    GLuint *program = &g_gl.programs[PROGRAM_BACKGROUND];

    LOG("Loading {Background-Program}");
    djgp_push_file(djp, PATH_TO_SHADER_DIRECTORY "Background.glsl");
    if (!djgp_to_gl(djp, 430, false, true, program)) {
        LOG("=> Failure <=");
        djgp_release(djp);

        return false;
    }
    djgp_release(djp);

    g_gl.uniforms[UNIFORM_BACKGROUND_SAMPLES_PER_PASS] =
        glGetUniformLocation(g_gl.programs[PROGRAM_BACKGROUND], "u_SamplesPerPass");

    ConfigureBackgroundProgram();

    return (glGetError() == GL_NO_ERROR);
}



// -----------------------------------------------------------------------------
/**
 * Load All Programs
 *
 */
bool LoadPrograms()
{
    bool success = true;

    if (success) success = LoadViewerProgram();
    if (success) success = LoadSphereProgram();
    if (success) success = LoadLightProgram();
    if (success) success = LoadBackgroundProgram();

    return success;
}


////////////////////////////////////////////////////////////////////////////////
// Texture Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load the Scene Framebuffer Textures
 *
 * Depending on the scene framebuffer AA mode, this function load 2 or
 * 3 textures. In FSAA mode, two RGBA32F and one DEPTH24_STENCIL8 textures
 * are created. In other modes, one RGBA32F and one DEPTH24_STENCIL8 textures
 * are created.
 */
bool LoadSceneFramebufferTexture()
{
    if (glIsTexture(g_gl.textures[TEXTURE_SCENE_COLOR_BUFFER]))
        glDeleteTextures(1, &g_gl.textures[TEXTURE_SCENE_COLOR_BUFFER]);
    if (glIsTexture(g_gl.textures[TEXTURE_SCENE_DEPTH_BUFFER]))
        glDeleteTextures(1, &g_gl.textures[TEXTURE_SCENE_DEPTH_BUFFER]);
    glGenTextures(1, &g_gl.textures[TEXTURE_SCENE_DEPTH_BUFFER]);
    glGenTextures(1, &g_gl.textures[TEXTURE_SCENE_COLOR_BUFFER]);

    switch (g_window.framebuffer.aa) {
        case AA_NONE:
            LOG("Loading {Scene-Z-Framebuffer-Texture}");
            glActiveTexture(GL_TEXTURE0 + TEXTURE_SCENE_DEPTH_BUFFER);
            glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_SCENE_DEPTH_BUFFER]);
            glTexStorage2D(GL_TEXTURE_2D,
                           1,
                           GL_DEPTH24_STENCIL8,
                           g_window.width,
                           g_window.height);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            LOG("Loading {Scene-RGBA-Framebuffer-Texture}");
            glActiveTexture(GL_TEXTURE0 + TEXTURE_SCENE_COLOR_BUFFER);
            glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_SCENE_COLOR_BUFFER]);
            glTexStorage2D(GL_TEXTURE_2D,
                           1,
                           GL_RGBA32F,
                           g_window.width,
                           g_window.height);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            break;
        case AA_MSAA2:
        case AA_MSAA4:
        case AA_MSAA8:
        case AA_MSAA16: {
            int32_t samples = 1u << g_window.framebuffer.aa;
            int32_t maxSamples;

            glGetIntegerv(GL_MAX_INTEGER_SAMPLES, &maxSamples);
            if (samples > maxSamples) {
                LOG("note: MSAA is %ix", maxSamples);
                samples = maxSamples;
            }
            LOG("Loading {Scene-MSAA-Z-Framebuffer-Texture}");
            glActiveTexture(GL_TEXTURE0 + TEXTURE_SCENE_DEPTH_BUFFER);
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE, g_gl.textures[TEXTURE_SCENE_DEPTH_BUFFER]);
            glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
                                      samples,
                                      GL_DEPTH24_STENCIL8,
                                      g_window.width,
                                      g_window.height,
                                      g_window.framebuffer.msaa.fixed);

            LOG("Loading {Scene-MSAA-RGBA-Framebuffer-Texture}");
            glActiveTexture(GL_TEXTURE0 + TEXTURE_SCENE_COLOR_BUFFER);
            glBindTexture(GL_TEXTURE_2D_MULTISAMPLE,
                          g_gl.textures[TEXTURE_SCENE_COLOR_BUFFER]);
            glTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE,
                                      samples,
                                      GL_RGBA32F,
                                      g_window.width,
                                      g_window.height,
                                      g_window.framebuffer.msaa.fixed);
        } break;
    }
    glActiveTexture(GL_TEXTURE0);

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load the Viewer Framebuffer Texture
 *
 * This loads an RGBA8 texture used as a color buffer for the back
 * framebuffer.
 */
bool LoadViewerFramebufferTexture()
{
    LOG("Loading {Viewer-Framebuffer-Texture}");
    if (glIsTexture(g_gl.textures[TEXTURE_VIEWER_COLOR_BUFFER]))
        glDeleteTextures(1, &g_gl.textures[TEXTURE_VIEWER_COLOR_BUFFER]);
    glGenTextures(1, &g_gl.textures[TEXTURE_VIEWER_COLOR_BUFFER]);

    glActiveTexture(GL_TEXTURE0 + TEXTURE_VIEWER_COLOR_BUFFER);
    glBindTexture(GL_TEXTURE_2D, g_gl.textures[TEXTURE_VIEWER_COLOR_BUFFER]);
    glTexStorage2D(GL_TEXTURE_2D,
                   1,
                   GL_RGBA8,
                   g_window.width,
                   g_window.height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glActiveTexture(GL_TEXTURE0);

    return (glGetError() == GL_NO_ERROR);
}

#define M00(m) m[0][0]
#define M01(m) m[0][1]
#define M02(m) m[0][2]
#define M03(m) m[0][3]
#define M10(m) m[1][0]
#define M11(m) m[1][1]
#define M12(m) m[1][2]
#define M13(m) m[1][3]
#define M20(m) m[2][0]
#define M21(m) m[2][1]
#define M22(m) m[2][2]
#define M23(m) m[2][3]
#define M30(m) m[3][0]
#define M31(m) m[3][1]
#define M32(m) m[3][2]
#define M33(m) m[3][3]

glm::mat3 adjugate(const glm::mat3 &m)
{
    glm::mat3 a;

    M00(a) = M11(m) * M22(m) - M12(m) * M21(m);
    M01(a) =-M01(m) * M22(m) + M02(m) * M21(m);
    M02(a) = M01(m) * M12(m) - M02(m) * M11(m);

    M10(a) =-M10(m) * M22(m) + M12(m) * M20(m);
    M11(a) = M00(m) * M22(m) - M02(m) * M20(m);
    M12(a) =-M00(m) * M12(m) + M02(m) * M10(m);

    M20(a) = M10(m) * M21(m) - M11(m) * M20(m);
    M21(a) =-M00(m) * M21(m) + M01(m) * M20(m);
    M22(a) = M00(m) * M11(m) - M01(m) * M10(m);

    return a;
}

glm::mat3 myInverse(glm::mat3 m)
{
    float det = glm::determinant(m);

    return (1.0f / det) * adjugate(m);
}

float urng()
{
    return rand() / (float)RAND_MAX;
}

// -----------------------------------------------------------------------------
/**
 */
bool LoadLtcTexture()
{
#include "ltc_isotropic_8x8.h"

    std::vector<float>tx1, tx2, tx3;
    const int32_t res = 8;

    tx1.resize(res * res * 3);
    tx2.resize(res * res * 3);
    tx3.resize(res * res * 3);

    for (int j = 0; j < res; ++j) {
        for (int i = 0; i < res; ++i) {

            tx1[0 + 3 * (i + res * j)] = ((float *)(isomats))[0 + 9 * (i + res * j)];
            tx1[1 + 3 * (i + res * j)] = ((float *)(isomats))[1 + 9 * (i + res * j)];
            tx1[2 + 3 * (i + res * j)] = ((float *)(isomats))[2 + 9 * (i + res * j)];

            tx2[0 + 3 * (i + res * j)] = ((float *)(isomats))[3 + 9 * (i + res * j)];
            tx2[1 + 3 * (i + res * j)] = ((float *)(isomats))[4 + 9 * (i + res * j)];
            tx2[2 + 3 * (i + res * j)] = ((float *)(isomats))[5 + 9 * (i + res * j)];

            tx3[0 + 3 * (i + res * j)] = ((float *)(isomats))[6 + 9 * (i + res * j)];
            tx3[1 + 3 * (i + res * j)] = ((float *)(isomats))[7 + 9 * (i + res * j)];
            tx3[2 + 3 * (i + res * j)] = ((float *)(isomats))[8 + 9 * (i + res * j)];
        }
    }

    LOG("Loading {Ltc-Texture}");
    if (glIsTexture(g_gl.textures[TEXTURE_LTC]))
        glDeleteTextures(1, &g_gl.textures[TEXTURE_LTC]);
    glGenTextures(1, &g_gl.textures[TEXTURE_LTC]);

    glActiveTexture(GL_TEXTURE0 + TEXTURE_LTC);
    glBindTexture(GL_TEXTURE_2D_ARRAY, g_gl.textures[TEXTURE_LTC]);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY,
                   1,
                   GL_RGB32F,
                   res,
                   res,
                   3);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                    0,
                    0, 0, 0,
                    res, res, 1,
                    GL_RGB, GL_FLOAT, &tx1[0]);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                    0,
                    0, 0, 1,
                    res, res, 1,
                    GL_RGB, GL_FLOAT, &tx2[0]);
    glTexSubImage3D(GL_TEXTURE_2D_ARRAY,
                    0,
                    0, 0, 2,
                    res, res, 1,
                    GL_RGB, GL_FLOAT, &tx3[0]);

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE0);

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 */
bool LoadLtcAnisoTexture()
{
#if 1
    #include "ltc_m_reparam.h"

    std::vector<float>mData;

    mData.resize(8 * 8 * 8 * 8 * 3);

    LOG("Loading {Ltc-Aniso-Texture}");
    if (glIsTexture(g_gl.textures[TEXTURE_LTC_ANISO_1])) {
        for (int i = 0; i < 3; ++i) {
            glDeleteTextures(1, &g_gl.textures[TEXTURE_LTC_ANISO_1 + i]);
        }
    }

    for (int i = 0; i < 3; ++i) {
        glGenTextures(1, &g_gl.textures[TEXTURE_LTC_ANISO_1 + i]);
    }

    for (int c = 0; c < 3; ++c) {
        for (int i4 = 0; i4 < 8; ++i4) {
            for (int i3 = 0; i3 < 8; ++i3) {
                for (int i2 = 0; i2 < 8; ++i2) {
                    for (int i1 = 0; i1 < 8; ++i1) {
                        mData[0 + 3 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = ((float *)(anisomats))[3 * c + 0 + 9 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))];
                        mData[1 + 3 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = ((float *)(anisomats))[3 * c + 1 + 9 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))];
                        mData[2 + 3 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = ((float *)(anisomats))[3 * c + 2 + 9 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))];
                    }
                }
            }
        }

        glActiveTexture(GL_TEXTURE0 + TEXTURE_LTC_ANISO_1 + c);
        glBindTexture(GL_TEXTURE_3D, g_gl.textures[TEXTURE_LTC_ANISO_1 + c]);
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGB32F, 8, 8, 64);
        glTexSubImage3D(GL_TEXTURE_3D,
                        0,
                        0, 0, 0,
                        8, 8, 64,
                        GL_RGB, GL_FLOAT, &mData[0]);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        //float borderColor[4] = {10, 10, 10, 10};
        //glTexParameterfv(GL_TEXTURE_3D, GL_TEXTURE_BORDER_COLOR, borderColor);
    }
#else
    std::vector<float>mData;

    mData.resize(8 * 8 * 8 * 8 * 4);

    LOG("Loading {Ltc-Aniso-Texture}");
    if (glIsTexture(g_gl.textures[TEXTURE_LTC_ANISO_1])) {
        for (int i = 0; i < 3; ++i) {
            glDeleteTextures(1, &g_gl.textures[TEXTURE_LTC_ANISO_1 + i]);
        }
    }

    for (int i = 0; i < 3; ++i) {
        glGenTextures(1, &g_gl.textures[TEXTURE_LTC_ANISO_1 + i]);
    }

    srand(1213);

    for (int c = 0; c < 3; ++c) {
        for (int i4 = 0; i4 < 8; ++i4) {
            for (int i3 = 0; i3 < 8; ++i3) {
                for (int i2 = 0; i2 < 8; ++i2) {
                    for (int i1 = 0; i1 < 8; ++i1) {
                        mData[0 + 4 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = urng();
                        mData[1 + 4 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = urng();
                        mData[2 + 4 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = urng();
                        mData[3 + 4 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = urng();
                    }
                }
            }
        }

        glActiveTexture(GL_TEXTURE0 + TEXTURE_LTC_ANISO_1 + c);
        glBindTexture(GL_TEXTURE_3D, g_gl.textures[TEXTURE_LTC_ANISO_1 + c]);
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA32F, 8, 8, 64);
        glTexSubImage3D(GL_TEXTURE_3D,
                        0,
                        0, 0, 0,
                        8, 8, 64,
                        GL_RGBA, GL_FLOAT, &mData[0]);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
#endif

    glActiveTexture(GL_TEXTURE0);

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load All Textures
 */
bool LoadTextures()
{
    bool success = true;

    if (success) success = LoadSceneFramebufferTexture();
    if (success) success = LoadViewerFramebufferTexture();
    if (success) success = LoadLtcTexture();
    if (success) success = LoadLtcAnisoTexture();

    return success;
}


////////////////////////////////////////////////////////////////////////////////
// Buffer Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load Sphere Data Buffers
 *
 * This procedure updates the transformations and the data of the spheres that
 * are used in the demo; it is updated each frame.
 */
bool LoadSphereBuffers()
{
    static bool first = true;
    struct Transform {
        glm::mat4 modelView,
                  modelViewInv,
                  modelViewProjection,
                  viewInv,
                  view,
                  model,
                  viewProjection,
                  dummy;
    } transform;

    if (first) {
        g_gl.streams[STREAM_TRANSFORM] = djgb_create(sizeof(transform));
        first = false;
    }

    // extract view and projection matrices
#if 1
    const glm::mat4 projection = glm::perspective(
        g_camera.fovy,
        (float)g_window.width / (float)g_window.height,
        g_camera.zNear,
        g_camera.zFar
    );
#else
    const glm::mat4 projection = glm::ortho(
        -1.0f, 1.0f, -1.0f, 1.0f, g_camera.zNear, g_camera.zFar
    );
#endif
    const glm::mat4 model = glm::rotate(glm::mat4(1.f), -90.0f, glm::vec3(1.0f, 0.0f, 0.0f));
    const glm::mat4 viewInv = InverseViewMatrix();
    const glm::mat4 view = glm::inverse(viewInv);

    // upload transformations
    transform.modelView  = view * model;
    transform.modelViewInv = glm::inverse(transform.modelView);
    transform.modelViewProjection = projection * transform.modelView;
    transform.viewProjection = projection * view;
    transform.viewInv = viewInv;
    transform.view = view;
    transform.model = model;

    // upload data
    djgb_to_gl(g_gl.streams[STREAM_TRANSFORM], (const void *)&transform, NULL);
    djgb_glbindrange(g_gl.streams[STREAM_TRANSFORM],
                     GL_UNIFORM_BUFFER,
                     STREAM_TRANSFORM);

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load Random Buffer
 *
 * This buffer holds the random samples used by the GLSL Monte Carlo integrator.
 * It should be updated at each frame. The random samples are generated using
 * the Marsaglia pseudo-random generator.
 */
uint32_t mrand() // Marsaglia random generator
{
    static uint32_t m_z = 1, m_w = 2;

    m_z = 36969u * (m_z & 65535u) + (m_z >> 16u);
    m_w = 18000u * (m_w & 65535u) + (m_w >> 16u);

    return ((m_z << 16u) + m_w);
}

bool LoadRandomBuffer()
{
    static bool first = true;
    float buffer[256];
    int32_t offset = 0;

    if (first) {
        g_gl.streams[STREAM_RANDOM] = djgb_create(sizeof(buffer));
        first = false;
    }

    for (int32_t i = 0; i < BUFFER_SIZE(buffer); ++i) {
        buffer[i] = (float)((double)mrand() / (double)0xFFFFFFFFu);
        assert(buffer[i] <= 1.f && buffer[i] >= 0.f);
    }

    djgb_to_gl(g_gl.streams[STREAM_RANDOM], (const void *)buffer, &offset);
    djgb_glbindrange(g_gl.streams[STREAM_RANDOM],
                     GL_UNIFORM_BUFFER,
                     STREAM_RANDOM);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Sphere Mesh Buffer
 *
 * This loads a vertex and an index buffer for a mesh.
 */
bool LoadSphereMeshBuffers()
{
    int32_t vertexCount, indexCount;
    djg_mesh *mesh = djgm_load_sphere(g_sphere.sphere.xTess,
                                      g_sphere.sphere.yTess);
    const djgm_vertex *vertices = djgm_get_vertices(mesh, &vertexCount);
    const uint16_t *indexes = djgm_get_triangles(mesh, &indexCount);

    if (glIsBuffer(g_gl.buffers[BUFFER_SPHERE_VERTICES]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_SPHERE_VERTICES]);
    if (glIsBuffer(g_gl.buffers[BUFFER_SPHERE_INDEXES]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_SPHERE_INDEXES]);

    LOG("Loading {Mesh-Vertex-Buffer}");
    glGenBuffers(1, &g_gl.buffers[BUFFER_SPHERE_VERTICES]);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_SPHERE_VERTICES]);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(djgm_vertex) * vertexCount,
                 (const void*)vertices,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    LOG("Loading {Mesh-Grid-Index-Buffer}");
    glGenBuffers(1, &g_gl.buffers[BUFFER_SPHERE_INDEXES]);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.buffers[BUFFER_SPHERE_INDEXES]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 sizeof(uint16_t) * indexCount,
                 (const void *)indexes,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    g_sphere.sphere.indexCount = indexCount;
    g_sphere.sphere.vertexCount = vertexCount;
    djgm_release(mesh);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Sphere Mesh Buffer
 *
 * This loads a vertex and an index buffer for a mesh.
 */
bool LoadLtcBuffer()
{
    #include "ltc-iso.inl"

    std::vector<float>mData;

    mData.resize(8 * 8 * 9);

    if (glIsBuffer(g_gl.buffers[BUFFER_LTC]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_LTC]);

    for (int i2 = 0; i2 < 8; ++i2) {
        for (int i1 = 0; i1 < 8; ++i1) {
            for (int c = 0; c < 9; ++c) {
                mData[c + 9 * (i1 + 8 * i2)] = ((float *)(isomats))[c + 9 * (i1 + 8 * i2)];
            }
        }
    }

    LOG("Loading {Ltc-Buffer}");
    glGenBuffers(1, &g_gl.buffers[BUFFER_LTC]);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_LTC]);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(isomats),
                 (const void*)&mData[0],
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LTC,
                     g_gl.buffers[BUFFER_LTC]);

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load Sphere Mesh Buffer
 *
 * This loads a vertex and an index buffer for a mesh.
 */
bool LoadLtcAnisoBuffer()
{
    #include "ltc_m_reparam.h"

    std::vector<float>mData;

    mData.resize(8 * 8 * 8 * 8 * 9);

    if (glIsBuffer(g_gl.buffers[BUFFER_LTC_ANISO]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_LTC_ANISO]);

    for (int i4 = 0; i4 < 8; ++i4) {
        for (int i3 = 0; i3 < 8; ++i3) {
            for (int i2 = 0; i2 < 8; ++i2) {
                for (int i1 = 0; i1 < 8; ++i1) {
                    for (int c = 0; c < 9; ++c) {
                        mData[c + 9 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = ((float *)(anisomats))[c + 9 * ( i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))];
                    }
                }
            }
        }
    }

    LOG("Loading {Ltc-Aniso-Buffer}");
    glGenBuffers(1, &g_gl.buffers[BUFFER_LTC_ANISO]);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_LTC_ANISO]);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(anisomats),
                 (const void*)&mData[0],
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LTC_ANISO,
                     g_gl.buffers[BUFFER_LTC_ANISO]);

    return (glGetError() == GL_NO_ERROR);
}

bool LoadLtcAniso2Buffer()
{
    #include "ltc-aniso-sym-l2.inl"

    if (glIsBuffer(g_gl.buffers[BUFFER_LTC_ANISO_L2]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_LTC_ANISO_L2]);

    LOG("Loading {Ltc-Aniso-Buffer}");
    glGenBuffers(1, &g_gl.buffers[BUFFER_LTC_ANISO_L2]);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_LTC_ANISO_L2]);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(anisomats),
                 (const void*)anisomats,
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LTC_ANISO_L2,
                     g_gl.buffers[BUFFER_LTC_ANISO_L2]);

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load Sphere Mesh Buffer
 *
 * This loads a vertex and an index buffer for a mesh.
 */
bool LoadLtcAmplitudeBuffer()
{
    #include "ltc-amplitude-sym-64k.inl"

    if (glIsBuffer(g_gl.buffers[BUFFER_LTC_AMPLITUDE]))
        glDeleteBuffers(1, &g_gl.buffers[BUFFER_LTC_AMPLITUDE]);

    std::vector<float>mData;

    mData.resize(8 * 8 * 8 * 8 * 9);

    for (int i4 = 0; i4 < 8; ++i4) {
        for (int i3 = 0; i3 < 8; ++i3) {
            for (int i2 = 0; i2 < 8; ++i2) {
                for (int i1 = 0; i1 < 8; ++i1) {
                    for (int c = 0; c < 1; ++c) {
                        mData[c + 1 * (i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))] = ((float *)(ltcamp))[c + 1 * ( i1 + 8 * (i2 + 8 * (i3 + 8 * i4)))];
                    }
                }
            }
        }
    }

    LOG("Loading {Ltc-Amplitude-Buffer}");
    glGenBuffers(1, &g_gl.buffers[BUFFER_LTC_AMPLITUDE]);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_LTC_AMPLITUDE]);
    glBufferData(GL_ARRAY_BUFFER,
                 sizeof(ltcamp),
                 (const void*)&mData[0],
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,
                     BUFFER_LTC_AMPLITUDE,
                     g_gl.buffers[BUFFER_LTC_AMPLITUDE]);

    return (glGetError() == GL_NO_ERROR);
}


// -----------------------------------------------------------------------------
/**
 * Load All Buffers
 *
 */
bool LoadBuffers()
{
    bool success = true;

    if (success) success = LoadLtcBuffer();
    if (success) success = LoadLtcAnisoBuffer();
    if (success) success = LoadLtcAniso2Buffer();
    if (success) success = LoadLtcAmplitudeBuffer();
    if (success) success = LoadRandomBuffer();
    if (success) success = LoadSphereMeshBuffers();

    return success;
}


////////////////////////////////////////////////////////////////////////////////
// Vertex Array Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load an Empty Vertex Array
 *
 * This will be used to draw procedural geometry, e.g., a fullscreen quad.
 */
bool LoadEmptyVertexArray()
{
    LOG("Loading {Empty-VertexArray}");
    if (glIsVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]))
        glDeleteVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_EMPTY]);

    glGenVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glBindVertexArray(0);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load Mesh Vertex Array
 *
 * This will be used to draw the sphere mesh loaded with the dj_opengl library.
 */
bool LoadSphereVertexArray()
{
    LOG("Loading {Mesh-VertexArray}");
    if (glIsVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]))
        glDeleteVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_SPHERE]);

    glGenVertexArrays(1, &g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glEnableVertexAttribArray(3);
    glBindBuffer(GL_ARRAY_BUFFER, g_gl.buffers[BUFFER_SPHERE_VERTICES]);
    glVertexAttribPointer(0, 4, GL_FLOAT, 0, sizeof(djgm_vertex),
                          BUFFER_OFFSET(0));
    glVertexAttribPointer(1, 4, GL_FLOAT, 0, sizeof(djgm_vertex),
                          BUFFER_OFFSET(4 * sizeof(float)));
    glVertexAttribPointer(2, 4, GL_FLOAT, 0, sizeof(djgm_vertex),
                          BUFFER_OFFSET(8 * sizeof(float)));
    glVertexAttribPointer(3, 4, GL_FLOAT, 0, sizeof(djgm_vertex),
                          BUFFER_OFFSET(12 * sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_gl.buffers[BUFFER_SPHERE_INDEXES]);
    glBindVertexArray(0);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Vertex Arrays
 *
 */
bool LoadVertexArrays()
{
    bool success = true;

    if (success) success = LoadEmptyVertexArray();
    if (success) success = LoadSphereVertexArray();

    return success;
}


////////////////////////////////////////////////////////////////////////////////
// Framebuffer Loading
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Load the Viewer Framebuffer
 *
 * This framebuffer contains the final image. It will be blitted to the
 * OpenGL window's backbuffer.
 */
bool LoadViewerFramebuffer()
{
    LOG("Loading {Viewer-Framebuffer}");
    if (glIsFramebuffer(g_gl.framebuffers[FRAMEBUFFER_VIEWER]))
        glDeleteFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_VIEWER]);

    glGenFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_VIEWER]);
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_VIEWER]);
    glFramebufferTexture2D(GL_FRAMEBUFFER,
                           GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D,
                           g_gl.textures[TEXTURE_VIEWER_COLOR_BUFFER],
                           0);

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (GL_FRAMEBUFFER_COMPLETE != glCheckFramebufferStatus(GL_FRAMEBUFFER)) {
        LOG("=> Failure <=");

        return false;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load the Scene Framebuffer
 *
 * This framebuffer is used to draw the 3D scene.
 * A single framebuffer is created, holding a color and Z buffer.
 * The scene writes directly to it.
 */
bool LoadSceneFramebuffer()
{
    LOG("Loading {Scene-Framebuffer}");
    if (glIsFramebuffer(g_gl.framebuffers[FRAMEBUFFER_SCENE]))
        glDeleteFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_SCENE]);

    glGenFramebuffers(1, &g_gl.framebuffers[FRAMEBUFFER_SCENE]);
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);

    if (g_window.framebuffer.aa >= AA_MSAA2 && g_window.framebuffer.aa <= AA_MSAA16) {
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D_MULTISAMPLE,
                               g_gl.textures[TEXTURE_SCENE_COLOR_BUFFER],
                               0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D_MULTISAMPLE,
                               g_gl.textures[TEXTURE_SCENE_DEPTH_BUFFER],
                               0);
    } else {
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D,
                               g_gl.textures[TEXTURE_SCENE_COLOR_BUFFER],
                               0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,
                               GL_DEPTH_STENCIL_ATTACHMENT,
                               GL_TEXTURE_2D,
                               g_gl.textures[TEXTURE_SCENE_DEPTH_BUFFER],
                               0);
    }

    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    if (GL_FRAMEBUFFER_COMPLETE != glCheckFramebufferStatus(GL_FRAMEBUFFER)) {
        LOG("=> Failure <=");

        return false;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return (glGetError() == GL_NO_ERROR);
}

// -----------------------------------------------------------------------------
/**
 * Load All Framebuffers
 *
 */
bool LoadFramebuffers()
{
    bool success = true;

    if (success) success = LoadViewerFramebuffer();
    if (success) success = LoadSceneFramebuffer();

    return success;
}


////////////////////////////////////////////////////////////////////////////////
// OpenGL Resource Loading
//
////////////////////////////////////////////////////////////////////////////////

bool ParseCommandLine(int argc, char **argv)
{
#define PARSE_SHADING_MODE(str, enumval)                     \
    else if (!strcmp("--integrator-" str, argv[i])) {           \
            g_sphere.ggx.integrator = enumval;   \
            LOG("Note: integrator set to " str "\n");  \
    }

    for (int i = 1; i < argc; ++i) {
        if (!strcmp("--frame-limit", argv[i])) {
            g_window.maxFrameID = atoi(argv[++i]);
            LOG("Note: frame limit set to %i\n", g_window.maxFrameID);
        } else if (!strcmp("--record", argv[i])) {
            g_window.recorder.isRunning = true;
            LOG("Note: recording enabled\n");
        } else if (!strcmp("--no-hud", argv[i])) {
            g_window.showHud = false;
            LOG("Note: HUD rendering disabled\n");
        } else if (!strcmp("--disable-progressive", argv[i])) {
            g_window.framebuffer.flags.progressive = false;
            LOG("Note: progressive rendering is disabled\n");
        } else if (!strcmp("--camera", argv[i])) {
            g_camera.fovy  = atof(argv[++i]);
            g_camera.zNear= atof(argv[++i]);
            g_camera.zFar  = atof(argv[++i]);
            g_camera.angles.theta = (atof(argv[++i]) / 180.0f);
            g_camera.angles.phi   = (atof(argv[++i]) / 360.0f);
            g_camera.radius       = atof(argv[++i]);
            LOG("Note: camera set to %f %f %f %f %f %f\n",
                g_camera.fovy, g_camera.zNear, g_camera.zFar,
                g_camera.angles.theta * 180.0f, g_camera.angles.phi * 360.0f, g_camera.radius);
        } else if (!strcmp("--samples-per-pass", argv[i])) {
            g_window.framebuffer.samplesPerPass = atoi(argv[++i]);
            LOG("Note: samples per pass set to %i\n", g_window.framebuffer.samplesPerPass);
        } else if (!strcmp("--samples-per-pixel", argv[i])) {
            g_window.framebuffer.samplesPerPixel = atoi(argv[++i]);
            LOG("Note: samples per pixel set to %i\n", g_window.framebuffer.samplesPerPixel);
        } else if (!strcmp("--alpha", argv[i])) {
            g_sphere.ggx.alpha.x = atof(argv[++i]);
            g_sphere.ggx.alpha.y = atof(argv[++i]);
            LOG("Note: alpha set to %.3f %.3f\n",
                g_sphere.ggx.alpha.x,
                g_sphere.ggx.alpha.y);
        } else if (!strcmp("--light", argv[i])) {
            g_light.intensity = atof(argv[++i]);
            g_light.width = atof(argv[++i]);
            g_light.height = atof(argv[++i]);
            g_light.theta = atof(argv[++i]) / 180.0f;
            g_light.phi = atof(argv[++i]) / 360.0f;
            g_light.distanceToOrigin = atof(argv[++i]);
            g_light.yRot = atof(argv[++i]) / 360.0f;
            g_light.zRot = atof(argv[++i]) / 360.0f;

            LOG("Note: light set to:\n"
                "- intensity: %.3f\n"
                "- width: %.3f\n"
                "- height: %.3f\n"
                "- theta: %.3f\n"
                "- phi: %.3f\n"
                "- r: %.3f\n"
                "- yRot: %.3f\n"
                "- zRot: %.3f\n",
                g_light.intensity,
                g_light.width,
                g_light.height,
                g_light.theta * 180.0f,
                g_light.phi * 360.0,
                g_light.distanceToOrigin,
                g_light.yRot * 360.0f,
                g_light.zRot * 360.0f);
        }
        PARSE_SHADING_MODE("ggx" , INTEGRATOR_GGX_MIS)
        PARSE_SHADING_MODE("ltc", INTEGRATOR_LTC)
        PARSE_SHADING_MODE("ltc-analytic", INTEGRATOR_LTC_ANALYTIC)
    }

#undef PARSE_SHADING_MODE

    return true;
}

bool Load(int argc, char **argv)
{
    bool success = ParseCommandLine(argc, argv);

    InitDebugOutput();
    if (success) success = LoadTextures();
    if (success) success = LoadBuffers();
    if (success) success = LoadFramebuffers();
    if (success) success = LoadVertexArrays();
    if (success) success = LoadPrograms();

    for (int i = 0; i < CLOCK_COUNT; ++i) {
        g_gl.clocks[i] = djgc_create();
    }

    return success;
}

void Release()
{
    int32_t i;

    for (i = 0; i < STREAM_COUNT; ++i)
        if (g_gl.streams[i])
            djgb_release(g_gl.streams[i]);
    for (i = 0; i < PROGRAM_COUNT; ++i)
        if (glIsProgram(g_gl.programs[i]))
            glDeleteProgram(g_gl.programs[i]);
    for (i = 0; i < TEXTURE_COUNT; ++i)
        if (glIsTexture(g_gl.textures[i]))
            glDeleteTextures(1, &g_gl.textures[i]);
    for (i = 0; i < BUFFER_COUNT; ++i)
        if (glIsBuffer(g_gl.buffers[i]))
            glDeleteBuffers(1, &g_gl.buffers[i]);
    for (i = 0; i < FRAMEBUFFER_COUNT; ++i)
        if (glIsFramebuffer(g_gl.framebuffers[i]))
            glDeleteFramebuffers(1, &g_gl.framebuffers[i]);
    for (i = 0; i < VERTEXARRAY_COUNT; ++i)
        if (glIsVertexArray(g_gl.vertexArrays[i]))
            glDeleteVertexArrays(1, &g_gl.vertexArrays[i]);
    for (i = 0; i < CLOCK_COUNT; ++i)
        if (g_gl.clocks[i])
            djgc_release(g_gl.clocks[i]);
}


////////////////////////////////////////////////////////////////////////////////
// OpenGL Rendering
//
////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
/**
 * Render the Scene
 *
 * This drawing pass renders the 3D scene to the framebuffer.
 */
void RenderScene_Progressive()
{
    // configure GL state
    glBindFramebuffer(GL_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_SCENE]);
    glViewport(0, 0, g_window.width, g_window.height);
    glDepthFunc(GL_LESS);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    if (g_window.framebuffer.flags.reset) {
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        g_window.framebuffer.passID = 0;
        g_window.framebuffer.flags.reset = false;
    }

    // enable blending only after the first is complete
    // (otherwise backfaces might be included in the rendering)
    if (g_window.framebuffer.passID > 0) {
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
    } else {
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
    }

    // stop progressive drawing once the desired sampling rate has been reached
    if (g_window.framebuffer.passID * g_window.framebuffer.samplesPerPass
        < g_window.framebuffer.samplesPerPixel) {
        LoadRandomBuffer();

        // draw sphere
        if (g_sphere.flags.showLines)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);

        glFinish();
        djgc_start(g_gl.clocks[CLOCK_RENDER]);
        glUseProgram(g_gl.programs[PROGRAM_SPHERE]);
        glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_SPHERE]);
        glDrawElements(GL_TRIANGLES,
                       g_sphere.sphere.indexCount,
                       GL_UNSIGNED_SHORT,
                       NULL);
        djgc_stop(g_gl.clocks[CLOCK_RENDER]);
        glFinish();

        glDisable(GL_CULL_FACE);
        glUseProgram(g_gl.programs[PROGRAM_LIGHT]);
        glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glEnable(GL_CULL_FACE);

        glUseProgram(g_gl.programs[PROGRAM_BACKGROUND]);
        glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        if (g_sphere.flags.showLines)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        ++g_window.framebuffer.passID;
    }

    // restore GL state
    if (g_window.framebuffer.passID > 0) {
        glDepthFunc(GL_LESS);
        glDisable(GL_BLEND);
    }
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
}

void RenderScene()
{
    LoadSphereBuffers();

    if (g_window.framebuffer.flags.progressive) {
        RenderScene_Progressive();
    } else {
        const uint32_t samplesPerPixel = g_window.framebuffer.samplesPerPixel;
        const uint32_t samplesPerPass = g_window.framebuffer.samplesPerPass;
        const uint32_t passCount = samplesPerPixel / samplesPerPass;

        for (uint32_t passID = 0; passID < passCount; ++passID) {
            RenderScene_Progressive();
        }
    }
}


// -----------------------------------------------------------------------------
/**
 * Blit the Scene Framebuffer and draw GUI
 *
 * This drawing pass blits the scene framebuffer with possible magnification
 * and renders the HUD and TweakBar.
 */
void ImguiSetAa()
{
    if (!LoadSceneFramebufferTexture() || !LoadSceneFramebuffer()
    || !LoadViewerProgram()) {
        LOG("=> Framebuffer config failed <=\n");
        abort();
    }
    g_window.framebuffer.flags.reset = true;
}

void RenderViewer()
{
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_VIEWER]);
    glViewport(0, 0, g_window.width, g_window.height);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);

    // post process the scene framebuffer
    glUseProgram(g_gl.programs[PROGRAM_VIEWER]);
    glBindVertexArray(g_gl.vertexArrays[VERTEXARRAY_EMPTY]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // draw HUD
    if (g_window.showHud) {
        glUseProgram(0);
        // ImGui
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        // Viewer Widgets
        ImGui::SetNextWindowPos(ImVec2(270, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(250, 120), ImGuiCond_FirstUseEver);
        ImGui::Begin("Framebuffer");
        {
            const char* aaItems[] = {
                "None",
                "MSAA x2",
                "MSAA x4",
                "MSAA x8",
                "MSAA x16"
            };
            if (ImGui::Combo("AA", &g_window.framebuffer.aa, aaItems, BUFFER_SIZE(aaItems)))
                ImguiSetAa();
            if (ImGui::Combo("MSAA", &g_window.framebuffer.msaa.fixed, "Fixed\0Random\0\0"))
                ImguiSetAa();
            ImGui::Checkbox("Progressive", &g_window.framebuffer.flags.progressive);
            if (g_window.framebuffer.flags.progressive) {
                ImGui::SameLine();
                if (ImGui::Button("Reset"))
                    g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::Button("Take Screenshot")) {
                static int cnt = 0;
                char buf[1024];

                snprintf(buf, 1024, "screenshot%03i", cnt);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
                djgt_save_glcolorbuffer_png(GL_FRONT, GL_RGBA, buf);
                ++cnt;
            }
            if (ImGui::Button("Record"))
                g_window.recorder.isRunning = !g_window.recorder.isRunning;
            if (g_window.recorder.isRunning) {
                ImGui::SameLine();
                ImGui::Text("Recording...");
            }
        }
        ImGui::End();

        // Camera Widgets
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(250, 120), ImGuiCond_FirstUseEver);
        ImGui::Begin("Camera");
        {
            if (ImGui::SliderFloat("FOVY", &g_camera.fovy, 1.0f, 179.0f))
                g_window.framebuffer.flags.reset = true;
            if (ImGui::SliderFloat("zNear", &g_camera.zNear, 0.001f, 100.f)) {
                if (g_camera.zNear >= g_camera.zFar)
                    g_camera.zNear = g_camera.zFar - 0.01f;
            }
            if (ImGui::SliderFloat("zFar", &g_camera.zFar, 1.f, 1500.f)) {
                if (g_camera.zFar <= g_camera.zNear)
                    g_camera.zFar = g_camera.zNear + 0.01f;
            }
        }
        ImGui::End();
        // Sphere
        ImGui::SetNextWindowPos(ImVec2(10, 140), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(250, 200), ImGuiCond_FirstUseEver);
        ImGui::Begin("Sphere");
        {
            if (ImGui::CollapsingHeader("Flags", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::Checkbox("Wireframe", &g_sphere.flags.showLines))
                    g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::CollapsingHeader("GGX", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* integrators[] = {
                    "GGX",
                    "GGX-Light",
                    "GGX-MIS",
                    "LTC",
                    "LTC-Analytic"
                };
                float progress = (float)g_window.framebuffer.passID
                               * g_window.framebuffer.samplesPerPass
                               / g_window.framebuffer.samplesPerPixel;
                if (g_window.framebuffer.flags.progressive)
                    ImGui::ProgressBar(progress);
                if (ImGui::Combo("Integrator", &g_sphere.ggx.integrator, integrators, BUFFER_SIZE(integrators))) {
                    LoadSphereProgram();
                    g_window.framebuffer.flags.reset = true;
                }
                if (ImGui::Checkbox("Isotropic", &g_sphere.ggx.isotropic)) {
                    if (g_sphere.ggx.isotropic) {
                        g_sphere.ggx.alpha.y = g_sphere.ggx.alpha.x;
                    }

                    ConfigureSphereProgram();
                    g_window.framebuffer.flags.reset = true;
                }

                if (g_sphere.ggx.isotropic) {
                    if (ImGui::SliderFloat("alpha", &g_sphere.ggx.alpha.x, 1e-3f, 1.0f)) {
                        g_sphere.ggx.alpha.y = g_sphere.ggx.alpha.x;
                        ConfigureSphereProgram();
                        g_window.framebuffer.flags.reset = true;
                    }
                } else {
                    if (ImGui::SliderFloat2("alpha", &g_sphere.ggx.alpha.x, 1e-3f, 1.0f)) {
                        ConfigureSphereProgram();
                        g_window.framebuffer.flags.reset = true;
                    }
                }
                if (ImGui::Checkbox("UseL2", &g_sphere.ggx.l2)) {
                    LoadSphereProgram();
                    g_window.framebuffer.flags.reset = true;
                }
            }
        }
        ImGui::End();

        // Light
        ImGui::SetNextWindowPos(ImVec2(10, 350), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(250, 220), ImGuiCond_FirstUseEver);
        ImGui::Begin("Light");
        {
            if (ImGui::SliderFloat("Intensity", &g_light.intensity, 0.0f, 10.0f)) {
                ConfigureSphereProgram();
                ConfigureLightProgram();
                g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::SliderFloat("Width", &g_light.width, 0.1f, 15.0f)) {
                ConfigureSphereProgram();
                ConfigureLightProgram();
                g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::SliderFloat("Height", &g_light.height, 0.1f, 15.0f)) {
                ConfigureSphereProgram();
                ConfigureLightProgram();
                g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::SliderFloat("Rotation Y", &g_light.yRot, 0.0f, 1.0f)) {
                ConfigureSphereProgram();
                ConfigureLightProgram();
                g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::SliderFloat("Rotation Z", &g_light.zRot, 0.0f, 1.0f)) {
                ConfigureSphereProgram();
                ConfigureLightProgram();
                g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::SliderFloat("Elevation", &g_light.theta, 0.0f, 1.0f)) {
                ConfigureSphereProgram();
                ConfigureLightProgram();
                g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::SliderFloat("Azimuth", &g_light.phi, 0.0f, 1.0f)) {
                ConfigureSphereProgram();
                ConfigureLightProgram();
                g_window.framebuffer.flags.reset = true;
            }
            if (ImGui::SliderFloat("Distance", &g_light.distanceToOrigin, 0.1f, 10.0f)) {
                ConfigureSphereProgram();
                ConfigureLightProgram();
                g_window.framebuffer.flags.reset = true;
            }
        }
        ImGui::End();

        // performances
        ImGui::SetNextWindowPos(ImVec2(530, 10), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(250, 80), ImGuiCond_FirstUseEver);
        ImGui::Begin("Performances");
        {
            double cpuDt, gpuDt;
            djgc_ticks(g_gl.clocks[CLOCK_RENDER], &cpuDt, &gpuDt);

            ImGui::Text("GPU_dt: %.3f %s",
                        gpuDt < 1. ? gpuDt * 1e3 : gpuDt,
                        gpuDt < 1. ? "ms" : " s");
        }
        ImGui::End();

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    }

    // restore state
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
}

// -----------------------------------------------------------------------------
/**
 * Blit the Composited Framebuffer to the Window Backbuffer
 *
 * Final drawing step: the composited framebuffer is blitted to the
 * OpenGL window backbuffer
 */
void Render()
{
    RenderScene();
    RenderViewer();

    glBindFramebuffer(GL_READ_FRAMEBUFFER, g_gl.framebuffers[FRAMEBUFFER_VIEWER]);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

    // blit scene framebuffer
    glBlitFramebuffer(0, 0, g_window.width, g_window.height,
                      0, 0, g_window.width, g_window.height,
                      GL_COLOR_BUFFER_BIT,
                      GL_NEAREST);

    // screen recording
    if (g_window.recorder.isRunning) {
        char name[64];

        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        sprintf(name, "capture_%02i_%09i",
                g_window.recorder.captureID,
                g_window.recorder.frameID);
        djgt_save_glcolorbuffer_bmp(GL_BACK, GL_RGB, name);
        ++g_window.recorder.frameID;
    }
}


////////////////////////////////////////////////////////////////////////////////

// -----------------------------------------------------------------------------
void
KeyboardCallback(
    GLFWwindow*,
    int key, int, int action, int
) {
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard)
        return;

    if (action == GLFW_PRESS) {
        switch (key) {
        case GLFW_KEY_ESCAPE:
            g_window.showHud = !g_window.showHud;
            break;
        case GLFW_KEY_C:
            if (g_window.recorder.isRunning) {
                g_window.recorder.frameID = 0;
                ++g_window.recorder.captureID;
            }
            g_window.recorder.isRunning = !g_window.recorder.isRunning;
            break;
        case GLFW_KEY_R:
            LoadBuffers();
            LoadPrograms();
            g_window.framebuffer.flags.reset = true;
            g_window.framebuffer.passID = 0;
            break;
        case GLFW_KEY_T: {
            char name[64], path[1024];
            static int frameID = 0;

            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            sprintf(name, "screenshot_%02i", frameID);
            sprintf(path, "./%s", name);
            djgt_save_glcolorbuffer_bmp(GL_FRONT, GL_RGBA, path);
            ++frameID;
        }
        default: break;
        }
    }
}

void MouseButtonCallback(GLFWwindow*, int, int, int)
{
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;
}

void MouseMotionCallback(GLFWwindow* window, double x, double y)
{
    static float x0 = 0, y0 = 0;
    const float dx = x - x0, dy = y - y0;

    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse)
        return;

    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        g_camera.angles.theta-= dy * 1e-3f;
        g_camera.angles.phi-= dx * 1e-3f;

        if (g_camera.angles.theta < 0.0f)
            g_camera.angles.theta = 0.0f;
        if (g_camera.angles.theta > 1.0f)
            g_camera.angles.theta = 1.0f;

        g_window.framebuffer.flags.reset = true;
    } else if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS) {
        g_camera.radius-= dx * 5e-3f;

        if (g_camera.radius < 1.0f)
            g_camera.radius = 1.0f;

        g_window.framebuffer.flags.reset = true;
    }

    //LOG("%f %f %f", g_camera.angles.theta, g_camera.angles.phi, g_camera.radius);

    x0 = x;
    y0 = y;
}

void MouseScrollCallback(GLFWwindow* window, double xoffset, double yoffset)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
    if (io.WantCaptureMouse)
        return;
}

void ResizeCallback(GLFWwindow* window, int width, int height)
{
    (void)window;

    g_window.width = width;
    g_window.height = height;
    g_window.framebuffer.flags.reset = true;

    LoadSceneFramebufferTexture();
    LoadSceneFramebuffer();
    LoadViewerFramebufferTexture();
    LoadViewerFramebuffer();
}

int main(int argc, char **argv)
{
    bool isVisible = true;

    for (int i = 0; i < argc; ++i) {
        if (!strcmp("--hidden", argv[i])) {
            isVisible = false;
        }
    }

    LOG("Loading {OpenGL Window}");
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, g_window.glversion.major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, g_window.glversion.minor);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, isVisible);
#ifndef NDEBUG
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GL_TRUE);
#endif
    g_window.handle = glfwCreateWindow(g_window.width,
                                       g_window.height,
                                       g_window.name,
                                       NULL, NULL);
    if (g_window.handle == NULL) {
        LOG("=> Failure <=");
        glfwTerminate();

        return -1;
    }
    glfwMakeContextCurrent(g_window.handle);
    glfwSetKeyCallback(g_window.handle, &KeyboardCallback);
    glfwSetCursorPosCallback(g_window.handle, &MouseMotionCallback);
    glfwSetMouseButtonCallback(g_window.handle, &MouseButtonCallback);
    glfwSetScrollCallback(g_window.handle, &MouseScrollCallback);
    glfwSetWindowSizeCallback(g_window.handle, &ResizeCallback);

    // load OpenGL functions
    LOG("Loading {OpenGL Functions}");
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        LOG("=> Failure <=");
        glfwTerminate();

        return -1;
    }

    // initialize ImGUI
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(g_window.handle, false);
    ImGui_ImplOpenGL3_Init("#version 450");

    // initialize
    LOG("Loading {Demo}");
    if (!Load(argc, argv)) {
        LOG("=> Failure <=");
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwTerminate();

        return -1;
    }

    // main loop
    while (!glfwWindowShouldClose(g_window.handle) && g_window.frameID != g_window.maxFrameID) {
        glfwPollEvents();
        Render();
        glfwSwapBuffers(g_window.handle);

        if (g_window.maxFrameID != ~0u)
            ++g_window.frameID;
    }

    // cleanup
    Release();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwTerminate();

    return 0;
}
