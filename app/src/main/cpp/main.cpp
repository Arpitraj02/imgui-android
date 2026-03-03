// main.cpp – NativeActivity entry point
// Initialises EGL, ImGui, then runs the render loop.

#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>

#include "imgui.h"
#include "imgui_backends/imgui_impl_android.h"
#include "imgui_backends/imgui_impl_opengl3.h"
#include "android_input.h"
#include "ui_demo.h"

#define LOG_TAG "ImGuiDemo"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ── EGL state ─────────────────────────────────────────────────────────────────
struct EGLState
{
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int        width   = 0;
    int        height  = 0;
};

static EGLState g_egl;
static bool     g_ImGuiInitialised = false;
static bool     g_Running          = true;

// ── EGL init/deinit ──────────────────────────────────────────────────────────
static bool EGLInit(ANativeWindow* window)
{
    g_egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (g_egl.display == EGL_NO_DISPLAY) { LOGE("eglGetDisplay failed"); return false; }

    if (!eglInitialize(g_egl.display, nullptr, nullptr))
    {
        LOGE("eglInitialize failed");
        return false;
    }

    // Prefer OpenGL ES 3.0, fallback to 2.0
    const EGLint attribs_gles3[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_BLUE_SIZE,  8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };
    const EGLint attribs_gles2[] = {
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_BLUE_SIZE,  8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
        EGL_DEPTH_SIZE, 16,
        EGL_NONE
    };

    EGLConfig config;
    EGLint    num_configs;
    if (!eglChooseConfig(g_egl.display, attribs_gles3, &config, 1, &num_configs)
        || num_configs == 0)
    {
        LOGI("ES3 config unavailable, falling back to ES2");
        if (!eglChooseConfig(g_egl.display, attribs_gles2, &config, 1, &num_configs)
            || num_configs == 0)
        {
            LOGE("eglChooseConfig failed");
            return false;
        }
    }

    EGLint format;
    eglGetConfigAttrib(g_egl.display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(window, 0, 0, format);

    g_egl.surface = eglCreateWindowSurface(g_egl.display, config, window, nullptr);
    if (g_egl.surface == EGL_NO_SURFACE)
    {
        LOGE("eglCreateWindowSurface failed: 0x%x", eglGetError());
        return false;
    }

    const EGLint ctx_attribs_v3[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    const EGLint ctx_attribs_v2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    g_egl.context = eglCreateContext(g_egl.display, config, EGL_NO_CONTEXT, ctx_attribs_v3);
    if (g_egl.context == EGL_NO_CONTEXT)
    {
        LOGI("ES3 context failed, trying ES2");
        g_egl.context = eglCreateContext(g_egl.display, config, EGL_NO_CONTEXT, ctx_attribs_v2);
    }
    if (g_egl.context == EGL_NO_CONTEXT)
    {
        LOGE("eglCreateContext failed: 0x%x", eglGetError());
        return false;
    }

    if (!eglMakeCurrent(g_egl.display, g_egl.surface, g_egl.surface, g_egl.context))
    {
        LOGE("eglMakeCurrent failed: 0x%x", eglGetError());
        return false;
    }

    eglQuerySurface(g_egl.display, g_egl.surface, EGL_WIDTH,  &g_egl.width);
    eglQuerySurface(g_egl.display, g_egl.surface, EGL_HEIGHT, &g_egl.height);
    LOGI("EGL surface: %dx%d", g_egl.width, g_egl.height);
    return true;
}

static void EGLDeinit()
{
    if (g_egl.display != EGL_NO_DISPLAY)
    {
        eglMakeCurrent(g_egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (g_egl.context != EGL_NO_CONTEXT) eglDestroyContext(g_egl.display, g_egl.context);
        if (g_egl.surface != EGL_NO_SURFACE) eglDestroySurface(g_egl.display, g_egl.surface);
        eglTerminate(g_egl.display);
    }
    g_egl = EGLState{};
}

// ── ImGui init ────────────────────────────────────────────────────────────────
static void ImGuiInit(android_app* app)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr; // disable imgui.ini on Android

    // Try to load custom font from assets
    bool font_loaded = false;
    if (app->activity->assetManager)
    {
        AAsset* asset = AAssetManager_open(
            app->activity->assetManager,
            "Roboto-Medium.ttf",
            AASSET_MODE_BUFFER);
        if (asset)
        {
            off_t size = AAsset_getLength(asset);
            void* buf  = IM_ALLOC(size);
            AAsset_read(asset, buf, size);
            AAsset_close(asset);

            // ImGui takes ownership of buf when TTFDataOwnedByAtlas is true (default)
            ImFontConfig cfg;
            cfg.FontDataOwnedByAtlas = true;
            io.Fonts->AddFontFromMemoryTTF(buf, (int)size, 18.0f, &cfg);
            font_loaded = true;
            LOGI("Loaded custom font Roboto-Medium.ttf");
        }
    }
    if (!font_loaded)
    {
        io.Fonts->AddFontDefault();
        LOGI("Using default ImGui font");
    }

    // Polished dark mod-menu style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding    = 8.0f;
    style.FrameRounding     = 4.0f;
    style.GrabRounding      = 4.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 8.0f;
    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.ItemSpacing       = ImVec2(8, 6);
    style.WindowPadding     = ImVec2(12, 12);
    style.FramePadding      = ImVec2(6, 4);

    // Accent colours: purple/violet theme
    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg]         = ImVec4(0.08f, 0.08f, 0.10f, 0.96f);
    colors[ImGuiCol_TitleBg]          = ImVec4(0.18f, 0.10f, 0.30f, 1.00f);
    colors[ImGuiCol_TitleBgActive]    = ImVec4(0.25f, 0.15f, 0.45f, 1.00f);
    colors[ImGuiCol_Header]           = ImVec4(0.30f, 0.18f, 0.55f, 0.60f);
    colors[ImGuiCol_HeaderHovered]    = ImVec4(0.38f, 0.24f, 0.65f, 0.80f);
    colors[ImGuiCol_HeaderActive]     = ImVec4(0.45f, 0.30f, 0.75f, 1.00f);
    colors[ImGuiCol_Button]           = ImVec4(0.30f, 0.18f, 0.55f, 0.70f);
    colors[ImGuiCol_ButtonHovered]    = ImVec4(0.40f, 0.26f, 0.68f, 0.90f);
    colors[ImGuiCol_ButtonActive]     = ImVec4(0.50f, 0.35f, 0.80f, 1.00f);
    colors[ImGuiCol_FrameBg]          = ImVec4(0.16f, 0.16f, 0.22f, 0.80f);
    colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.24f, 0.24f, 0.35f, 0.90f);
    colors[ImGuiCol_FrameBgActive]    = ImVec4(0.32f, 0.22f, 0.50f, 1.00f);
    colors[ImGuiCol_CheckMark]        = ImVec4(0.70f, 0.50f, 1.00f, 1.00f);
    colors[ImGuiCol_SliderGrab]       = ImVec4(0.55f, 0.38f, 0.85f, 1.00f);
    colors[ImGuiCol_SliderGrabActive] = ImVec4(0.70f, 0.55f, 1.00f, 1.00f);
    colors[ImGuiCol_Tab]              = ImVec4(0.20f, 0.12f, 0.38f, 0.80f);
    colors[ImGuiCol_TabHovered]       = ImVec4(0.38f, 0.24f, 0.65f, 0.90f);
    colors[ImGuiCol_TabActive]        = ImVec4(0.30f, 0.20f, 0.55f, 1.00f);
    colors[ImGuiCol_Separator]        = ImVec4(0.40f, 0.28f, 0.60f, 0.50f);
    colors[ImGuiCol_Text]             = ImVec4(0.92f, 0.90f, 1.00f, 1.00f);

    ImGui_ImplAndroid_Init(app);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    g_ImGuiInitialised = true;
    LOGI("ImGui initialised");
}

static void ImGuiDeinit()
{
    if (!g_ImGuiInitialised) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    g_ImGuiInitialised = false;
}

// ── android_app event callback ────────────────────────────────────────────────
static void HandleAppCmd(android_app* app, int32_t cmd)
{
    switch (cmd)
    {
    case APP_CMD_INIT_WINDOW:
        LOGI("APP_CMD_INIT_WINDOW");
        if (app->window)
        {
            if (!EGLInit(app->window))
            {
                LOGE("EGL init failed – cannot render");
                return;
            }
            if (!g_ImGuiInitialised)
                ImGuiInit(app);
        }
        break;

    case APP_CMD_TERM_WINDOW:
        LOGI("APP_CMD_TERM_WINDOW");
        ImGuiDeinit();
        EGLDeinit();
        break;

    case APP_CMD_WINDOW_RESIZED:
    case APP_CMD_CONFIG_CHANGED:
        if (g_egl.display != EGL_NO_DISPLAY && g_egl.surface != EGL_NO_SURFACE)
        {
            eglQuerySurface(g_egl.display, g_egl.surface, EGL_WIDTH,  &g_egl.width);
            eglQuerySurface(g_egl.display, g_egl.surface, EGL_HEIGHT, &g_egl.height);
        }
        break;

    case APP_CMD_GAINED_FOCUS:
        LOGI("APP_CMD_GAINED_FOCUS");
        break;

    case APP_CMD_LOST_FOCUS:
        LOGI("APP_CMD_LOST_FOCUS");
        break;

    case APP_CMD_DESTROY:
        LOGI("APP_CMD_DESTROY");
        g_Running = false;
        break;

    default:
        break;
    }
}

static int32_t HandleInputEvent(android_app* /*app*/, AInputEvent* event)
{
    return ImGui_ImplAndroid_HandleInputEvent(event);
}

// ── Main entry ────────────────────────────────────────────────────────────────
void android_main(android_app* app)
{
    app->onAppCmd     = HandleAppCmd;
    app->onInputEvent = HandleInputEvent;

    g_Running = true;

    while (g_Running)
    {
        // Poll events
        int               events;
        android_poll_source* source;
        while (ALooper_pollAll(
            (g_egl.display != EGL_NO_DISPLAY) ? 0 : -1,
            nullptr, &events, (void**)&source) >= 0)
        {
            if (source) source->process(app, source);
            if (app->destroyRequested)
            {
                g_Running = false;
                break;
            }
        }

        if (!g_Running) break;

        // Skip rendering if EGL is not ready
        if (g_egl.display == EGL_NO_DISPLAY || !g_ImGuiInitialised) continue;

        // New ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplAndroid_NewFrame();
        ImGui::NewFrame();

        // Build the UI
        UiDemo_Draw(app, g_egl.display, g_egl.surface);

        // Render
        ImGui::Render();
        glViewport(0, 0, g_egl.width, g_egl.height);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        eglSwapBuffers(g_egl.display, g_egl.surface);
    }

    ImGuiDeinit();
    EGLDeinit();
}
