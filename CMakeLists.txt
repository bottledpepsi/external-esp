cmake_minimum_required(VERSION 3.16)
project(McEsp LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "mcesp is a Windows-only project (Win32 + Direct2D).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(mcesp
  src/main.cpp src/Logger.cpp src/Json.cpp src/Presets.cpp src/EspConfig.cpp
  src/FriendList.cpp src/ProfileManager.cpp src/Wire.cpp src/SkinCache.cpp
  src/ProcessManager.cpp src/AgentInjector.cpp src/AgentSupervisor.cpp src/StreamIngest.cpp src/Token.cpp src/Recorder.cpp src/EspRec.cpp src/Replay.cpp src/UdpListener.cpp
  src/Gfx.cpp src/EspRenderer.cpp src/ConfigUi.cpp src/EspOverlay.cpp)

# Console subsystem on purpose (logs are visible in the terminal): do NOT add -mwindows.
target_link_libraries(mcesp PRIVATE d2d1 dwrite dwmapi winmm gdi32 user32 ws2_32 winhttp windowscodecs ole32 uuid bcrypt)

# Default OFF: a fully static link (-static) fails on newer MinGW-w64 toolchains such as CLion 2026's bundled
# GCC 15 (undefined __intrinsic_setjmpex / __ms_vsnprintf from libpthread.a / libmsvcrt.a). The default links
# libgcc/libstdc++ statically and uses libwinpthread-1.dll, which is copied next to the exe below.
option(ESP_STATIC "Fully static link (single exe). Works on some toolchains only." OFF)
if(MINGW)
  target_compile_options(mcesp PRIVATE -Wall -Wextra -O2)
  if(ESP_STATIC)
    target_link_options(mcesp PRIVATE -static)
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

# The Java agent (build it first: agent/build.bat) must sit next to the exe.
if(EXISTS ${CMAKE_SOURCE_DIR}/agent/esp-agent.jar)
  add_custom_command(TARGET mcesp POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/agent/esp-agent.jar $<TARGET_FILE_DIR:mcesp>/esp-agent.jar)
else()
  message(WARNING "agent/esp-agent.jar not found - run agent/build.bat (needs a JDK) before building.")
endif()

# Host-side unit tests for the platform-independent core (config/JSON/friends/profiles/wire/camera/smoother).
# Not part of the Windows build; run on any machine:  see tests/unit_tests.cpp
