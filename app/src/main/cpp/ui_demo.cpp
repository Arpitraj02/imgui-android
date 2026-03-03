// ui_demo.cpp – Polished mod-menu style ImGui demo UI for Android
// Demonstrates: tabs, toggles, sliders, keybind UI, color picker,
//               style editor, and a screenshot button.

#include "ui_demo.h"

#include "imgui.h"
#include <android/log.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

// ── PNG writing (single-header, minimal) ─────────────────────────────────────
// We use a very small helper to write a raw RGBA PNG using zlib via Android's
// built-in libz.  Include <zlib.h> from NDK.
#include <zlib.h>

#define LOG_TAG "UiDemo"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ── Screenshot helper ─────────────────────────────────────────────────────────
static std::string GetScreenshotPath(android_app* app)
{
    if (!app || !app->activity) return "";
    // Use app-specific external files dir (no permissions needed on API 29+)
    const char* ext_dir = app->activity->externalDataPath;
    if (!ext_dir || ext_dir[0] == '\0')
        ext_dir = app->activity->internalDataPath;
    if (!ext_dir) return "";

    time_t now = time(nullptr);
    char name[256];
    snprintf(name, sizeof(name), "%s/screenshot_%ld.png", ext_dir, (long)now);
    return std::string(name);
}

// Write a 4-byte big-endian uint32
static void WriteBE32(std::vector<uint8_t>& buf, uint32_t v)
{
    buf.push_back((v >> 24) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >>  8) & 0xFF);
    buf.push_back((v      ) & 0xFF);
}

static uint32_t CRC32(const uint8_t* data, size_t len)
{
    return crc32(crc32(0L, Z_NULL, 0), data, (uInt)len);
}

static void WritePNGChunk(std::vector<uint8_t>& out,
                           const char tag[4],
                           const std::vector<uint8_t>& data)
{
    WriteBE32(out, (uint32_t)data.size());
    size_t tag_start = out.size();
    out.insert(out.end(), tag, tag + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t crc = CRC32(out.data() + tag_start, 4 + data.size());
    WriteBE32(out, crc);
}

static bool SavePNG(const std::string& path, int w, int h,
                     const std::vector<uint8_t>& rgba)
{
    // PNG signature
    std::vector<uint8_t> out;
    const uint8_t sig[] = {137,80,78,71,13,10,26,10};
    out.insert(out.end(), sig, sig + 8);

    // IHDR
    std::vector<uint8_t> ihdr(13);
    ihdr[0]  = (w >> 24) & 0xFF; ihdr[1]  = (w >> 16) & 0xFF;
    ihdr[2]  = (w >>  8) & 0xFF; ihdr[3]  = (w      ) & 0xFF;
    ihdr[4]  = (h >> 24) & 0xFF; ihdr[5]  = (h >> 16) & 0xFF;
    ihdr[6]  = (h >>  8) & 0xFF; ihdr[7]  = (h      ) & 0xFF;
    ihdr[8]  = 8;  // bit depth
    ihdr[9]  = 2;  // colour type: RGB (we'll convert RGBA→RGB for simplicity)
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    WritePNGChunk(out, "IHDR", ihdr);

    // IDAT: filter + deflate each row
    // Use colour type 6 (RGBA) instead to preserve alpha
    // Correct: rebuild with colour type 6
    // (Redo IHDR)
    ihdr[9] = 6; // RGBA
    out.clear();
    out.insert(out.end(), sig, sig + 8);
    WritePNGChunk(out, "IHDR", ihdr);

    // Raw scanlines with filter byte 0 (None)
    std::vector<uint8_t> raw;
    raw.reserve((size_t)(h * (1 + w * 4)));
    for (int row = h - 1; row >= 0; --row) // flip Y (GL reads bottom-up)
    {
        raw.push_back(0); // filter None
        const uint8_t* src = rgba.data() + row * w * 4;
        raw.insert(raw.end(), src, src + w * 4);
    }

    // Compress
    uLongf compressed_size = compressBound((uLong)raw.size());
    std::vector<uint8_t> compressed(compressed_size);
    if (compress2(compressed.data(), &compressed_size,
                   raw.data(), (uLong)raw.size(), Z_DEFAULT_COMPRESSION) != Z_OK)
    {
        LOGE("PNG compress failed");
        return false;
    }
    compressed.resize(compressed_size);
    WritePNGChunk(out, "IDAT", compressed);

    // IEND
    WritePNGChunk(out, "IEND", {});

    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { LOGE("Cannot open %s for writing", path.c_str()); return false; }
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    LOGI("Screenshot saved: %s", path.c_str());
    return true;
}

static void TakeScreenshot(android_app* app, int w, int h)
{
    std::vector<uint8_t> pixels((size_t)(w * h * 4));
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    std::string path = GetScreenshotPath(app);
    if (path.empty())
    {
        LOGE("Cannot determine screenshot path");
        return;
    }
    SavePNG(path, w, h, pixels);
}

// ── UI state ──────────────────────────────────────────────────────────────────
static bool  s_enable_aimbot     = false;
static bool  s_enable_esp        = false;
static bool  s_enable_wallhack   = false;
static float s_fov_scale         = 1.0f;
static float s_smoothing         = 0.5f;
static int   s_esp_mode          = 0;   // 0=box, 1=skeleton, 2=both
static float s_esp_color[4]      = {0.2f, 0.8f, 1.0f, 1.0f};
static bool  s_show_style_editor = false;
static char  s_keybind_buf[32]   = "V";
static bool  s_waiting_keybind   = false;
static std::string s_status_msg;

// ── Main draw ─────────────────────────────────────────────────────────────────
void UiDemo_Draw(android_app* app, EGLDisplay /*display*/, EGLSurface /*surface*/)
{
    ImGuiIO& io = ImGui::GetIO();
    int screen_w = (int)io.DisplaySize.x;
    int screen_h = (int)io.DisplaySize.y;

    // Center the window on first use
    ImGui::SetNextWindowPos(
        ImVec2(screen_w * 0.5f, screen_h * 0.5f),
        ImGuiCond_Once,
        ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_Once);
    ImGui::SetNextWindowBgAlpha(0.95f);

    ImGui::Begin("ImGui Android Demo", nullptr,
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar);

    // ── Header ────────────────────────────────────────────────────────────────
    ImGui::TextColored(ImVec4(0.7f, 0.5f, 1.0f, 1.0f), "  Android Mod-Menu Demo");
    ImGui::Separator();
    ImGui::Spacing();

    // ── Tabs ──────────────────────────────────────────────────────────────────
    if (ImGui::BeginTabBar("MainTabs"))
    {
        // ── Combat tab ────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Combat"))
        {
            ImGui::Spacing();
            ImGui::Checkbox("Aimbot", &s_enable_aimbot);
            if (s_enable_aimbot)
            {
                ImGui::Indent();
                ImGui::SliderFloat("FOV scale", &s_fov_scale,   0.1f, 5.0f,  "%.2f");
                ImGui::SliderFloat("Smoothing", &s_smoothing,   0.0f, 1.0f,  "%.2f");

                ImGui::Text("Keybind:");
                ImGui::SameLine();
                if (s_waiting_keybind)
                {
                    ImGui::Button("[ press key ]", ImVec2(120, 0));
                    // On Android we get key events via onInputEvent; for demo just show placeholder
                    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                        s_waiting_keybind = false;
                }
                else
                {
                    if (ImGui::Button(s_keybind_buf, ImVec2(120, 0)))
                        s_waiting_keybind = true;
                }
                ImGui::Unindent();
            }
            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        // ── Visuals tab ───────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Visuals"))
        {
            ImGui::Spacing();
            ImGui::Checkbox("ESP", &s_enable_esp);
            ImGui::Checkbox("Wallhack", &s_enable_wallhack);

            if (s_enable_esp)
            {
                ImGui::Indent();
                const char* esp_modes[] = {"Box", "Skeleton", "Box+Skeleton"};
                ImGui::Combo("ESP Mode", &s_esp_mode, esp_modes, IM_ARRAYSIZE(esp_modes));
                ImGui::ColorEdit4("ESP Color", s_esp_color);
                ImGui::Unindent();
            }
            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        // ── Misc tab ──────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("Misc"))
        {
            ImGui::Spacing();

            if (ImGui::Button("Screenshot", ImVec2(130, 36)))
            {
                TakeScreenshot(app, screen_w, screen_h);
                s_status_msg = "Screenshot saved!";
            }
            ImGui::SameLine();
            if (ImGui::Button("Style Editor", ImVec2(130, 36)))
                s_show_style_editor = !s_show_style_editor;

            if (!s_status_msg.empty())
            {
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "%s", s_status_msg.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextDisabled("Display: %dx%d", screen_w, screen_h);
            ImGui::TextDisabled("FPS: %.1f", io.Framerate);
            ImGui::Spacing();
            ImGui::EndTabItem();
        }

        // ── About tab ─────────────────────────────────────────────────────────
        if (ImGui::BeginTabItem("About"))
        {
            ImGui::Spacing();
            ImGui::TextWrapped(
                "ImGui Android Sample\n\n"
                "Demonstrates Dear ImGui on Android via NativeActivity "
                "and OpenGL ES 3.0.\n\n"
                "Tap the UI elements to interact. Drag windows to move them.\n\n"
                "Screenshot button saves a PNG to app external storage.");
            ImGui::Spacing();
            ImGui::TextDisabled("ImGui v%s", IMGUI_VERSION);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();

    // ── Optional style editor ─────────────────────────────────────────────────
    if (s_show_style_editor)
    {
        ImGui::SetNextWindowSize(ImVec2(380, 440), ImGuiCond_Once);
        ImGui::Begin("Style Editor", &s_show_style_editor);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }
}
