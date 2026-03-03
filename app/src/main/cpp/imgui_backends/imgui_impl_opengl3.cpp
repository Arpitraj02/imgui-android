// imgui_impl_opengl3.cpp
// OpenGL ES 3.0 renderer backend for Dear ImGui (Android)
// Targets: OpenGL ES 3.0 with GLSL "#version 300 es" shaders.
// Falls back to ES 2.0 / "#version 100" if ES3 is unavailable.

#include "imgui_impl_opengl3.h"
#include "imgui.h"

#include <GLES3/gl3.h>
#include <android/log.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define LOG_TAG "ImGui_GL3"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

// ── OpenGL data ───────────────────────────────────────────────────────────────
struct ImGui_ImplOpenGL3_Data
{
    GLuint GlVersion;
    char   GlslVersionString[32];
    GLuint FontTexture;
    GLuint ShaderHandle;
    GLint  AttribLocationTex;
    GLint  AttribLocationProjMtx;
    GLuint AttribLocationVtxPos;
    GLuint AttribLocationVtxUV;
    GLuint AttribLocationVtxColor;
    GLuint VboHandle;
    GLuint ElementsHandle;
    GLsizeiptr VboSize;
    GLsizeiptr ElementsSize;
    bool   HasPolygonMode;

    ImGui_ImplOpenGL3_Data() { memset((void*)this, 0, sizeof(*this)); }
};

static ImGui_ImplOpenGL3_Data* ImGui_ImplOpenGL3_GetBackendData()
{
    return ImGui::GetCurrentContext()
               ? (ImGui_ImplOpenGL3_Data*)ImGui::GetIO().BackendRendererUserData
               : nullptr;
}

// ── Helper: compile shader ─────────────────────────────────────────────────
static GLuint CreateShader(GLenum type, const GLchar* src)
{
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint status = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status)
    {
        char buf[512];
        glGetShaderInfoLog(shader, sizeof(buf), nullptr, buf);
        LOGE("Shader compile error: %s", buf);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool ImGui_ImplOpenGL3_Init(const char* glsl_version)
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == nullptr && "Already initialised!");

    ImGui_ImplOpenGL3_Data* bd = IM_NEW(ImGui_ImplOpenGL3_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName     = "imgui_impl_opengl3_gles3";
    io.BackendFlags           |= ImGuiBackendFlags_RendererHasVtxOffset;

    if (glsl_version == nullptr)
        glsl_version = "#version 300 es";
    IM_ASSERT(strlen(glsl_version) + 2 < sizeof(bd->GlslVersionString));
    snprintf(bd->GlslVersionString, sizeof(bd->GlslVersionString), "%s\n", glsl_version);

    // Query GL version
    const char* ver_str = (const char*)glGetString(GL_VERSION);
    LOGI("GL Version: %s", ver_str ? ver_str : "(null)");

    ImGui_ImplOpenGL3_CreateDeviceObjects();
    return true;
}

void ImGui_ImplOpenGL3_Shutdown()
{
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();
    IM_ASSERT(bd != nullptr);
    ImGui_ImplOpenGL3_DestroyDeviceObjects();
    ImGuiIO& io = ImGui::GetIO();
    io.BackendRendererName     = nullptr;
    io.BackendRendererUserData = nullptr;
    io.BackendFlags           &= ~ImGuiBackendFlags_RendererHasVtxOffset;
    IM_DELETE(bd);
}

void ImGui_ImplOpenGL3_NewFrame()
{
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();
    IM_ASSERT(bd != nullptr);
    if (!bd->ShaderHandle)
        ImGui_ImplOpenGL3_CreateDeviceObjects();
}

static void SetupRenderState(ImDrawData* draw_data, int fb_width, int fb_height,
                              GLuint vertex_array_object)
{
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();

    glEnable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_SCISSOR_TEST);

    glViewport(0, 0, (GLsizei)fb_width, (GLsizei)fb_height);

    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    const float ortho_projection[4][4] =
    {
        { 2.0f/(R-L),   0.0f,         0.0f,  0.0f },
        { 0.0f,         2.0f/(T-B),   0.0f,  0.0f },
        { 0.0f,         0.0f,        -1.0f,  0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),  0.0f,  1.0f },
    };

    glUseProgram(bd->ShaderHandle);
    glUniform1i(bd->AttribLocationTex, 0);
    glUniformMatrix4fv(bd->AttribLocationProjMtx, 1, GL_FALSE, &ortho_projection[0][0]);

    glBindVertexArray(vertex_array_object);
    glBindBuffer(GL_ARRAY_BUFFER, bd->VboHandle);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bd->ElementsHandle);

    glEnableVertexAttribArray(bd->AttribLocationVtxPos);
    glEnableVertexAttribArray(bd->AttribLocationVtxUV);
    glEnableVertexAttribArray(bd->AttribLocationVtxColor);
    glVertexAttribPointer(bd->AttribLocationVtxPos,
        2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
        (GLvoid*)IM_OFFSETOF(ImDrawVert, pos));
    glVertexAttribPointer(bd->AttribLocationVtxUV,
        2, GL_FLOAT, GL_FALSE, sizeof(ImDrawVert),
        (GLvoid*)IM_OFFSETOF(ImDrawVert, uv));
    glVertexAttribPointer(bd->AttribLocationVtxColor,
        4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(ImDrawVert),
        (GLvoid*)IM_OFFSETOF(ImDrawVert, col));
}

void ImGui_ImplOpenGL3_RenderDrawData(ImDrawData* draw_data)
{
    int fb_width  = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    if (fb_width <= 0 || fb_height <= 0) return;

    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();

    // Backup GL state
    GLint last_program; glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
    GLint last_texture;  glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);
    GLint last_array_buffer; glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &last_array_buffer);
    GLint last_vertex_array; glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &last_vertex_array);
    GLint last_viewport[4]; glGetIntegerv(GL_VIEWPORT, last_viewport);
    GLint last_scissor[4];  glGetIntegerv(GL_SCISSOR_BOX, last_scissor);
    GLboolean last_blend          = glIsEnabled(GL_BLEND);
    GLboolean last_cull_face      = glIsEnabled(GL_CULL_FACE);
    GLboolean last_depth_test     = glIsEnabled(GL_DEPTH_TEST);
    GLboolean last_scissor_test   = glIsEnabled(GL_SCISSOR_TEST);

    GLuint vao = 0;
    glGenVertexArrays(1, &vao);

    SetupRenderState(draw_data, fb_width, fb_height, vao);

    ImVec2 clip_off   = draw_data->DisplayPos;
    ImVec2 clip_scale = draw_data->FramebufferScale;

    for (int n = 0; n < draw_data->CmdListsCount; ++n)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];

        glBindBuffer(GL_ARRAY_BUFFER, bd->VboHandle);
        GLsizeiptr vtx_size = (GLsizeiptr)cmd_list->VtxBuffer.Size * (int)sizeof(ImDrawVert);
        if (bd->VboSize < vtx_size)
        {
            bd->VboSize = vtx_size;
            glBufferData(GL_ARRAY_BUFFER, bd->VboSize, nullptr, GL_STREAM_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, vtx_size, cmd_list->VtxBuffer.Data);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, bd->ElementsHandle);
        GLsizeiptr idx_size = (GLsizeiptr)cmd_list->IdxBuffer.Size * (int)sizeof(ImDrawIdx);
        if (bd->ElementsSize < idx_size)
        {
            bd->ElementsSize = idx_size;
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, bd->ElementsSize, nullptr, GL_STREAM_DRAW);
        }
        glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, idx_size, cmd_list->IdxBuffer.Data);

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; ++cmd_i)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != nullptr)
            {
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    SetupRenderState(draw_data, fb_width, fb_height, vao);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x,
                                (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y) continue;

                glScissor((GLint)clip_min.x,
                           (GLint)((float)fb_height - clip_max.y),
                           (GLsizei)(clip_max.x - clip_min.x),
                           (GLsizei)(clip_max.y - clip_min.y));

                glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)pcmd->GetTexID());
                glDrawElementsBaseVertex(
                    GL_TRIANGLES,
                    (GLsizei)pcmd->ElemCount,
                    sizeof(ImDrawIdx) == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT,
                    (void*)(intptr_t)(pcmd->IdxOffset * sizeof(ImDrawIdx)),
                    (GLint)pcmd->VtxOffset);
            }
        }
    }

    glDeleteVertexArrays(1, &vao);

    // Restore GL state
    glUseProgram(last_program);
    glBindTexture(GL_TEXTURE_2D, last_texture);
    glBindBuffer(GL_ARRAY_BUFFER, last_array_buffer);
    glBindVertexArray(last_vertex_array);
    glViewport(last_viewport[0], last_viewport[1],
               (GLsizei)last_viewport[2], (GLsizei)last_viewport[3]);
    glScissor(last_scissor[0], last_scissor[1],
              (GLsizei)last_scissor[2], (GLsizei)last_scissor[3]);
    if (last_blend)       glEnable(GL_BLEND);       else glDisable(GL_BLEND);
    if (last_cull_face)   glEnable(GL_CULL_FACE);   else glDisable(GL_CULL_FACE);
    if (last_depth_test)  glEnable(GL_DEPTH_TEST);  else glDisable(GL_DEPTH_TEST);
    if (last_scissor_test)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
}

bool ImGui_ImplOpenGL3_CreateFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();

    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

    GLint last_texture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &last_texture);

    glGenTextures(1, &bd->FontTexture);
    glBindTexture(GL_TEXTURE_2D, bd->FontTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    io.Fonts->SetTexID((ImTextureID)(intptr_t)bd->FontTexture);

    glBindTexture(GL_TEXTURE_2D, last_texture);
    return true;
}

void ImGui_ImplOpenGL3_DestroyFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();
    if (bd->FontTexture)
    {
        glDeleteTextures(1, &bd->FontTexture);
        io.Fonts->SetTexID(0);
        bd->FontTexture = 0;
    }
}

bool ImGui_ImplOpenGL3_CreateDeviceObjects()
{
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();

    const char* vertex_shader_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform mat4 ProjMtx;\n"
        "in vec2 Position;\n"
        "in vec2 UV;\n"
        "in vec4 Color;\n"
        "out vec2 Frag_UV;\n"
        "out vec4 Frag_Color;\n"
        "void main() {\n"
        "    Frag_UV    = UV;\n"
        "    Frag_Color = Color;\n"
        "    gl_Position = ProjMtx * vec4(Position.xy, 0.0, 1.0);\n"
        "}\n";

    const char* fragment_shader_src =
        "#version 300 es\n"
        "precision mediump float;\n"
        "uniform sampler2D Texture;\n"
        "in vec2 Frag_UV;\n"
        "in vec4 Frag_Color;\n"
        "out vec4 Out_Color;\n"
        "void main() {\n"
        "    Out_Color = Frag_Color * texture(Texture, Frag_UV.st);\n"
        "}\n";

    GLuint vert = CreateShader(GL_VERTEX_SHADER,   vertex_shader_src);
    GLuint frag = CreateShader(GL_FRAGMENT_SHADER, fragment_shader_src);
    if (!vert || !frag) return false;

    bd->ShaderHandle = glCreateProgram();
    glAttachShader(bd->ShaderHandle, vert);
    glAttachShader(bd->ShaderHandle, frag);
    glLinkProgram(bd->ShaderHandle);
    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint status = 0;
    glGetProgramiv(bd->ShaderHandle, GL_LINK_STATUS, &status);
    if (!status)
    {
        char buf[512];
        glGetProgramInfoLog(bd->ShaderHandle, sizeof(buf), nullptr, buf);
        LOGE("Shader link error: %s", buf);
        return false;
    }

    bd->AttribLocationTex     = glGetUniformLocation(bd->ShaderHandle, "Texture");
    bd->AttribLocationProjMtx = glGetUniformLocation(bd->ShaderHandle, "ProjMtx");
    bd->AttribLocationVtxPos   = (GLuint)glGetAttribLocation(bd->ShaderHandle, "Position");
    bd->AttribLocationVtxUV    = (GLuint)glGetAttribLocation(bd->ShaderHandle, "UV");
    bd->AttribLocationVtxColor = (GLuint)glGetAttribLocation(bd->ShaderHandle, "Color");

    glGenBuffers(1, &bd->VboHandle);
    glGenBuffers(1, &bd->ElementsHandle);

    ImGui_ImplOpenGL3_CreateFontsTexture();
    return true;
}

void ImGui_ImplOpenGL3_DestroyDeviceObjects()
{
    ImGui_ImplOpenGL3_Data* bd = ImGui_ImplOpenGL3_GetBackendData();
    if (bd->VboHandle)      { glDeleteBuffers(1, &bd->VboHandle);      bd->VboHandle = 0; }
    if (bd->ElementsHandle) { glDeleteBuffers(1, &bd->ElementsHandle); bd->ElementsHandle = 0; }
    if (bd->ShaderHandle)   { glDeleteProgram(bd->ShaderHandle);       bd->ShaderHandle = 0; }
    ImGui_ImplOpenGL3_DestroyFontsTexture();
}
