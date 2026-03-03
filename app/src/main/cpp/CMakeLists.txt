cmake_minimum_required(VERSION 3.22)
project(imgui_demo)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── ImGui (submodule) ──────────────────────────────────────────────────────────
set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/../../../../third_party/imgui)

set(IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/imgui_demo.cpp
)

# ── Backend sources (local copies/adaptations) ────────────────────────────────
set(BACKEND_SOURCES
    ${CMAKE_SOURCE_DIR}/imgui_backends/imgui_impl_android.cpp
    ${CMAKE_SOURCE_DIR}/imgui_backends/imgui_impl_opengl3.cpp
)

# ── Application sources ───────────────────────────────────────────────────────
set(APP_SOURCES
    ${CMAKE_SOURCE_DIR}/main.cpp
    ${CMAKE_SOURCE_DIR}/android_input.cpp
    ${CMAKE_SOURCE_DIR}/ui_demo.cpp
)

add_library(imgui_demo SHARED
    ${IMGUI_SOURCES}
    ${BACKEND_SOURCES}
    ${APP_SOURCES}
)

target_include_directories(imgui_demo PRIVATE
    ${IMGUI_DIR}
    ${CMAKE_SOURCE_DIR}/imgui_backends
    ${CMAKE_SOURCE_DIR}
)

# Android system libraries
find_library(log-lib log)
find_library(android-lib android)
find_library(EGL-lib EGL)
find_library(GLESv3-lib GLESv3)
find_library(GLESv2-lib GLESv2)

target_link_libraries(imgui_demo
    ${log-lib}
    ${android-lib}
    ${EGL-lib}
    ${GLESv3-lib}
    ${GLESv2-lib}
    android_native_app_glue
)

# android_native_app_glue (comes with NDK)
find_path(NATIVE_APP_GLUE_DIR android_native_app_glue.h
    HINTS ${ANDROID_NDK}/sources/android/native_app_glue)

if(NATIVE_APP_GLUE_DIR)
    add_library(android_native_app_glue STATIC
        ${NATIVE_APP_GLUE_DIR}/android_native_app_glue.c)
    target_include_directories(android_native_app_glue PUBLIC ${NATIVE_APP_GLUE_DIR})
    target_link_libraries(android_native_app_glue ${log-lib} ${android-lib})
endif()

target_compile_definitions(imgui_demo PRIVATE
    IMGUI_IMPL_OPENGL_ES3
    IMGUI_IMPL_OPENGL_LOADER_CUSTOM
)

# Export ANativeActivity_onCreate so NativeActivity finds the lib
set_target_properties(imgui_demo PROPERTIES
    LINK_FLAGS "-u ANativeActivity_onCreate"
)
