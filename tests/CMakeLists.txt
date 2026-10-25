# Host-side tests for the platform-independent core (config/JSON/friends/profiles/wire/camera/smoother).
# A separate CMake project on purpose: the main project is Windows-only (Direct2D), this one builds with any C++20
# compiler - Linux g++/clang, or MinGW on Windows (CI builds it in both places).
#
#   cmake -S tests -B build/tests && cmake --build build/tests -j && ctest --test-dir build/tests --output-on-failure
#
# (tests/run-tests.sh does exactly that.)
cmake_minimum_required(VERSION 3.16)
project(McEspTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(ESP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
find_package(Threads REQUIRED)

# The part of src/ that has no Windows dependency (Paths.h falls back to the current directory off Windows).
add_library(esp_core STATIC
  ${ESP_SRC}/Json.cpp ${ESP_SRC}/Presets.cpp ${ESP_SRC}/EspConfig.cpp ${ESP_SRC}/FriendList.cpp
  ${ESP_SRC}/ProfileManager.cpp ${ESP_SRC}/Wire.cpp ${ESP_SRC}/Logger.cpp)
target_include_directories(esp_core PUBLIC ${ESP_SRC})
target_link_libraries(esp_core PUBLIC Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(esp_core PUBLIC -Wall -Wextra -O1 -g)
endif()

enable_testing()

# Adds a test executable and runs it through run_clean.cmake: every run starts in an empty directory with a fresh
# copy of the executable, because the tests write esp_config.json / friends.json / profiles next to the exe
# (Windows) or into the working directory (elsewhere) and must not see files from a previous run.
function(esp_add_test name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE esp_core)
  add_test(NAME ${name}
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:${name}> -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/run/${name}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/run_clean.cmake)
endfunction()

esp_add_test(unit_tests unit_tests.cpp)
