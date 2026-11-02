cmake_minimum_required(VERSION 3.16)
project(McEsp LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "mcesp is a Windows-only project (Win32 + Direct2D).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mcesp
  src/main.cpp src/Logger.cpp src/Json.cpp src/AgentJar.cpp src/AgentJarWin.cpp src/Hotkeys.cpp src/HotkeyRegistry.cpp src/Tunables.cpp src/ConfigStore.cpp src/ConfigSchema.cpp src/LegacyImport.cpp src/Presets.cpp src/EspConfig.cpp
  src/FriendList.cpp src/ProfileManager.cpp src/Wire.cpp src/SkinCache.cpp
  src/ProcessManager.cpp src/AgentInjector.cpp src/AgentSupervisor.cpp src/StreamIngest.cpp src/Token.cpp src/Diagnostics.cpp src/DiagnosticsApp.cpp src/SysInfo.cpp src/Clipboard.cpp src/Recorder.cpp src/EspRec.cpp src/Replay.cpp src/UdpListener.cpp src/SelfTestApp.cpp src/DpiInfo.cpp src/WindowTracker.cpp src/PacingTestApp.cpp
  src/Gfx.cpp src/EspRenderer.cpp src/ConfigUi.cpp src/EspOverlay.cpp)

# Build id for the diagnostics report: `git describe` at configure time ("dev" when there is no git or no commit).
find_package(Git QUIET)
set(ESP_VERSION "dev")
if(GIT_FOUND)
  execute_process(COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                  OUTPUT_VARIABLE _esp_ver OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET RESULT_VARIABLE _esp_rc)
  if(_esp_rc EQUAL 0 AND _esp_ver)
    set(ESP_VERSION "${_esp_ver}")
  endif()
endif()
target_compile_definitions(mcesp PRIVATE ESP_VERSION="${ESP_VERSION}")

# Console subsystem on purpose (logs are visible in the terminal): do NOT add -mwindows.
target_link_libraries(mcesp PRIVATE d2d1 dwrite dwmapi winmm gdi32 user32 ws2_32 winhttp windowscodecs ole32 uuid bcrypt)

# Default OFF: a fully static link (-static) fails on newer MinGW-w64 toolchains such as CLion 2026's bundled
# GCC 15 (undefined __intrinsic_setjmpex / __ms_vsnprintf from libpthread.a / libmsvcrt.a). The default links
# libgcc/libstdc++ statically and uses libwinpthread-1.dll, which is copied next to the exe below.
option(ESP_STATIC "Fully static link (single exe). Works on some toolchains only." OFF)
# Default ON: link ONLY libwinpthread statically (not -static as a whole), so the exe needs no libwinpthread-1.dll and is one file with the embedded
# agent. It is tried at configure time with a program that really uses std::thread / mutex / condition_variable; if this toolchain cannot link it,
# the build silently falls back to the old behaviour (dynamic winpthread, DLL copied next to the exe) and says so.
# --whole-archive matters: libstdc++'s thread support refers to pthread functions through weak symbols, and a static libwinpthread that is not
# pulled in whole leaves them null (std::thread then throws at run time).
option(ESP_STATIC_WINPTHREAD "Link libwinpthread statically so no DLL has to be shipped" ON)
set(ESP_WINPTHREAD_LINK "-Wl,-Bstatic,--whole-archive,-lwinpthread,--no-whole-archive,-Bdynamic")
if(MINGW)
  target_compile_options(mcesp PRIVATE -Wall -Wextra -O2)
  set(_esp_static_pthread OFF)
  if(ESP_STATIC_WINPTHREAD AND NOT ESP_STATIC)
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-std=c++20")
    set(CMAKE_REQUIRED_LINK_OPTIONS -static-libgcc -static-libstdc++ ${ESP_WINPTHREAD_LINK})
    check_cxx_source_compiles("
      #include <thread>
      #include <mutex>
      #include <condition_variable>
      int main() { std::mutex m; std::condition_variable cv; bool go = false;
        std::jthread t([&] { std::lock_guard<std::mutex> l(m); go = true; cv.notify_all(); });
        std::unique_lock<std::mutex> l(m); cv.wait(l, [&] { return go; }); return 0; }" ESP_CAN_STATIC_WINPTHREAD)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(ESP_CAN_STATIC_WINPTHREAD)
      set(_esp_static_pthread ON)
    else()
      message(STATUS "libwinpthread cannot be linked statically with this toolchain: libwinpthread-1.dll will be copied next to the exe instead")
    endif()
  endif()
  if(ESP_STATIC)
    target_link_options(mcesp PRIVATE -static)
  elseif(_esp_static_pthread)
    target_link_options(mcesp PRIVATE -static-libgcc -static-libstdc++ ${ESP_WINPTHREAD_LINK})
    message(STATUS "libwinpthread is linked statically: mcesp.exe needs no DLL")
  else()
    target_link_options(mcesp PRIVATE -static-libgcc -static-libstdc++)
    # dynamic winpthread: ship libwinpthread-1.dll next to the exe
    get_filename_component(_cxx_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(EXISTS "${_cxx_dir}/libwinpthread-1.dll")
      add_custom_command(TARGET mcesp POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${_cxx_dir}/libwinpthread-1.dll" $<TARGET_FILE_DIR:mcesp>/libwinpthread-1.dll)
    endif()
  endif()
else()
  message(WARNING "Only MinGW-w64 GCC has been tested.")
endif()

# The Java agent (build it first: agent/build.bat or agent/build.sh) is embedded into the exe as a resource (ESPAGENT, RCDATA) and extracted at
# start-up to %LOCALAPPDATA%\\external-esp\\agent (see src/AgentJar.h), so the exe is one file. It is also copied next to the exe as before, for
# builds that are run with an older exe or for --jar.
set(ESP_AGENT_JAR ${CMAKE_SOURCE_DIR}/agent/esp-agent.jar)
if(EXISTS ${ESP_AGENT_JAR})
  set(ESP_AGENT_RC ${CMAKE_BINARY_DIR}/agent_resource.rc)
  file(WRITE ${ESP_AGENT_RC} "ESPAGENT RCDATA \"${ESP_AGENT_JAR}\"\n") # forward slashes: windres runs the C preprocessor over this file
  target_sources(mcesp PRIVATE ${ESP_AGENT_RC})
  set_source_files_properties(${ESP_AGENT_RC} PROPERTIES OBJECT_DEPENDS ${ESP_AGENT_JAR}) # relink when the jar changes
  add_custom_command(TARGET mcesp POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${ESP_AGENT_JAR} $<TARGET_FILE_DIR:mcesp>/esp-agent.jar)
else()
  message(WARNING "agent/esp-agent.jar not found - run agent/build.bat (needs a JDK) before building. The exe is built WITHOUT the embedded agent.")
endif()

# Host-side unit tests for the platform-independent core (config/JSON/friends/profiles/wire/camera/smoother).
# Not part of the Windows build; run on any machine:  see tests/unit_tests.cpp
