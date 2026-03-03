// imgui_impl_android.cpp
// Android NativeActivity platform backend for Dear ImGui
// Adapted for OpenGL ES 3.0 / NativeActivity

#include "imgui_impl_android.h"
#include "imgui.h"

#include <android/input.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <sys/time.h>
#include <time.h>

#define LOG_TAG "ImGui_Android"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static android_app* g_App             = nullptr;
static double       g_Time            = 0.0;
static bool         g_MouseDown[3]    = {false, false, false};
static int32_t      g_ActivePointerId = -1;

bool ImGui_ImplAndroid_Init(android_app* app)
{
    g_App = app;
    g_Time = 0.0;
    IMGUI_CHECKVERSION();

    ImGuiIO& io = ImGui::GetIO();

    // Let ImGui allocate clipboard internally; we won't implement native clipboard for now.
    io.BackendPlatformName = "imgui_impl_android";

    // We handle mouse-button events ourselves
    io.BackendFlags |= ImGuiBackendFlags_HasMouseCursors;

    return true;
}

void ImGui_ImplAndroid_Shutdown()
{
    g_App = nullptr;
}

static double GetTimeSeconds()
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void ImGui_ImplAndroid_NewFrame()
{
    ImGuiIO& io = ImGui::GetIO();

    // Display size from the window
    if (g_App && g_App->window)
    {
        int w = ANativeWindow_getWidth(g_App->window);
        int h = ANativeWindow_getHeight(g_App->window);
        io.DisplaySize = ImVec2((float)w, (float)h);
        io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    }

    // Delta time
    double current_time = GetTimeSeconds();
    io.DeltaTime = (g_Time > 0.0) ? (float)(current_time - g_Time) : (1.0f / 60.0f);
    if (io.DeltaTime <= 0.0f) io.DeltaTime = 1.0f / 60.0f;
    g_Time = current_time;
}

int32_t ImGui_ImplAndroid_HandleInputEvent(const AInputEvent* input_event)
{
    ImGuiIO& io = ImGui::GetIO();

    int32_t event_type = AInputEvent_getType(input_event);

    if (event_type == AINPUT_EVENT_TYPE_MOTION)
    {
        int32_t action       = AMotionEvent_getAction(input_event);
        int32_t action_type  = action & AMOTION_EVENT_ACTION_MASK;
        int32_t pointer_idx  = (action & AMOTION_EVENT_ACTION_POINTER_INDEX_MASK)
                                >> AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT;

        int32_t pointer_id   = AMotionEvent_getPointerId(input_event, pointer_idx);
        float   x            = AMotionEvent_getX(input_event, pointer_idx);
        float   y            = AMotionEvent_getY(input_event, pointer_idx);

        switch (action_type)
        {
        case AMOTION_EVENT_ACTION_DOWN:
        case AMOTION_EVENT_ACTION_POINTER_DOWN:
            if (g_ActivePointerId < 0 || pointer_id == g_ActivePointerId)
            {
                g_ActivePointerId = pointer_id;
                io.AddMousePosEvent(x, y);
                io.AddMouseButtonEvent(0, true);
            }
            break;

        case AMOTION_EVENT_ACTION_UP:
        case AMOTION_EVENT_ACTION_POINTER_UP:
        case AMOTION_EVENT_ACTION_CANCEL:
            if (pointer_id == g_ActivePointerId)
            {
                io.AddMousePosEvent(x, y);
                io.AddMouseButtonEvent(0, false);
                g_ActivePointerId = -1;
            }
            break;

        case AMOTION_EVENT_ACTION_MOVE:
        {
            // Track primary pointer (the one we latched)
            int32_t num_pointers = (int32_t)AMotionEvent_getPointerCount(input_event);
            for (int32_t i = 0; i < num_pointers; ++i)
            {
                if (AMotionEvent_getPointerId(input_event, i) == g_ActivePointerId)
                {
                    float mx = AMotionEvent_getX(input_event, i);
                    float my = AMotionEvent_getY(input_event, i);
                    io.AddMousePosEvent(mx, my);
                    break;
                }
            }

            // Two-finger vertical drag → vertical scroll
            if (num_pointers >= 2)
            {
                float y0 = AMotionEvent_getY(input_event, 0);
                float y1 = AMotionEvent_getY(input_event, 1);
                // Simple heuristic: feed average delta as scroll
                (void)y0; (void)y1;
            }
            break;
        }

        default:
            break;
        }
        return 1;
    }

    if (event_type == AINPUT_EVENT_TYPE_KEY)
    {
        int32_t action   = AKeyEvent_getAction(input_event);
        int32_t key_code = AKeyEvent_getKeyCode(input_event);
        bool    down     = (action == AKEY_EVENT_ACTION_DOWN);

        // Map common Android key codes to ImGui keys
        ImGuiKey imgui_key = ImGuiKey_None;
        switch (key_code)
        {
        case AKEYCODE_BACK:       imgui_key = ImGuiKey_Escape;     break;
        case AKEYCODE_DEL:        imgui_key = ImGuiKey_Backspace;  break;
        case AKEYCODE_ENTER:      imgui_key = ImGuiKey_Enter;      break;
        case AKEYCODE_DPAD_UP:    imgui_key = ImGuiKey_UpArrow;    break;
        case AKEYCODE_DPAD_DOWN:  imgui_key = ImGuiKey_DownArrow;  break;
        case AKEYCODE_DPAD_LEFT:  imgui_key = ImGuiKey_LeftArrow;  break;
        case AKEYCODE_DPAD_RIGHT: imgui_key = ImGuiKey_RightArrow; break;
        default: break;
        }

        if (imgui_key != ImGuiKey_None)
            io.AddKeyEvent(imgui_key, down);

        // Forward printable characters
        if (down)
        {
            int32_t meta = AKeyEvent_getMetaState(input_event);
            // Use getUnicodeChar via JNI would be ideal; for now skip unicode forwarding
            (void)meta;
        }

        return 1;
    }

    return 0;
}
