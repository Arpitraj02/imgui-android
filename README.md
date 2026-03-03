# ImGui Android Sample

A complete, working Android sample application demonstrating [Dear ImGui](https://github.com/ocornut/imgui) on Android using NativeActivity, EGL, and OpenGL ES 3.0.

## Features

- **NativeActivity** – pure C++ entry point, no Java UI code required.
- **OpenGL ES 3.0** rendering via ImGui's OpenGL3 backend (falls back to GLES2 if ES3 is unavailable).
- **Touch input** – single-touch mouse simulation, drag, and basic multi-touch gesture forwarding.
- **Polished mod-menu UI** – tabs, toggles, sliders, keybind demo, color/style editor, custom font support.
- **Screenshot button** – captures the GL framebuffer to a PNG file on external storage.
- **CI** – GitHub Actions matrix builds for `armeabi-v7a`, `arm64-v8a`, `x86`, `x86_64`; uploads `.so` artifacts and a debug APK.

## Project Structure

```
.
├── app/
│   ├── AndroidManifest.xml
│   ├── build.gradle
│   └── src/main/
│       ├── cpp/
│       │   ├── CMakeLists.txt
│       │   ├── main.cpp              # NativeActivity entry, EGL, main loop
│       │   ├── android_input.cpp     # Touch/key → ImGui input
│       │   ├── ui_demo.cpp           # Polished demo UI (mod-menu)
│       │   └── imgui_backends/
│       │       ├── imgui_impl_android.h/cpp
│       │       └── imgui_impl_opengl3.h/cpp
│       ├── assets/
│       │   └── Roboto-Medium.ttf     # Bundled font
│       └── java/                     # (placeholder; NativeActivity needs no Java code)
├── third_party/
│   └── imgui/                        # git submodule → ocornut/imgui (docking branch)
├── gradle/wrapper/
├── build.gradle
├── settings.gradle
├── gradle.properties
├── gradlew / gradlew.bat
├── .github/workflows/android-ci.yml
├── .gitmodules
├── LICENSE
└── README.md
```

## Prerequisites

| Tool | Recommended version |
|------|-------------------|
| Android Studio | Hedgehog+ |
| Android NDK | r25c (25.2.9519653) |
| CMake | 3.22+ |
| Gradle | 8.x (via wrapper) |
| JDK | 17 |

## Building Locally

### 1. Clone with submodules

```bash
git clone --recurse-submodules https://github.com/Arpitraj02/imgui-android.git
cd imgui-android
```

If you already cloned without `--recurse-submodules`:

```bash
git submodule update --init --recursive
```

### 2. Build native library for a single ABI (CMake only)

```bash
# Replace ARM64 with your target ABI
ABI=arm64-v8a
NDK=$ANDROID_NDK_ROOT   # e.g. ~/Android/Sdk/ndk/25.2.9519653

cmake -S app/src/main/cpp \
      -B build/$ABI \
      -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=$ABI \
      -DANDROID_PLATFORM=android-21 \
      -DANDROID_STL=c++_shared \
      -DCMAKE_BUILD_TYPE=Release

cmake --build build/$ABI -- -j$(nproc)
```

The `.so` will be at `build/$ABI/libimgui_demo.so`.

### 3. Build the debug APK (Gradle)

```bash
./gradlew assembleDebug
```

The APK is at `app/build/outputs/apk/debug/app-debug.apk`.

### 4. Install on device/emulator

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.example.imguiandroid/.MainActivity
```

## Screenshot Feature

Tap the **Screenshot** button in the demo UI. The PNG is saved to:

```
/sdcard/Android/data/com.example.imguiandroid/files/screenshot_<timestamp>.png
```

Retrieve it with:

```bash
adb pull /sdcard/Android/data/com.example.imguiandroid/files/
```

## CI Artifacts

After each push/PR the GitHub Actions workflow uploads:

| Artifact | Contents |
|----------|----------|
| `native-libs-<abi>.zip` | `libimgui_demo.so` for that ABI |
| `debug-apk` | `app-debug.apk` (fat APK, all ABIs) |

Download with the GitHub CLI:

```bash
gh run download <run-id> --name debug-apk
```

## License

MIT – see [LICENSE](LICENSE).

Dear ImGui is © Omar Cornut, MIT License – see [third_party/imgui/LICENSE.txt](third_party/imgui/LICENSE.txt).
