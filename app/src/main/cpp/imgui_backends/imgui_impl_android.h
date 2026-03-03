// imgui_impl_android.h
// Android NativeActivity platform backend for Dear ImGui
// Adapted for OpenGL ES 3.0 / NativeActivity

#pragma once
#include "imgui.h"

struct AInputEvent;
struct android_app;

IMGUI_IMPL_API bool ImGui_ImplAndroid_Init(struct android_app* app);
IMGUI_IMPL_API void ImGui_ImplAndroid_Shutdown();
IMGUI_IMPL_API void ImGui_ImplAndroid_NewFrame();
IMGUI_IMPL_API int32_t ImGui_ImplAndroid_HandleInputEvent(const AInputEvent* input_event);
