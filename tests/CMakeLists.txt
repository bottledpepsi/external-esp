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

# Sanitizers (GCC/Clang):  -DESP_SANITIZE=address,undefined   or   -DESP_SANITIZE=thread
#   address,undefined also enables float-cast-overflow (NOT part of -fsanitize=undefined, and it is what found the nan -> int cast in the
#   wire parser) and aborts on the first finding. thread cannot be combined with address.
set(ESP_SANITIZE "" CACHE STRING "Sanitizers to build the tests with: address,undefined | thread | empty for none")
if(ESP_SANITIZE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "ESP_SANITIZE needs GCC or Clang")
  endif()
  if(ESP_SANITIZE MATCHES "thread" AND ESP_SANITIZE MATCHES "address")
    message(FATAL_ERROR "ESP_SANITIZE: thread cannot be combined with address")
  endif()
  set(_san "${ESP_SANITIZE}")
  if(_san MATCHES "undefined")
    string(APPEND _san ",float-cast-overflow")
  endif()
  add_compile_options(-fsanitize=${_san} -fno-sanitize-recover=all -fno-omit-frame-pointer -O1 -g)
  add_link_options(-fsanitize=${_san})
  # more rounds for the fuzzer, the one test where extra work finds extra bugs
  set(ESP_FUZZ_ITERS_ENV "ESP_FUZZ_ITERS=200000")
  message(STATUS "Sanitizers: ${_san}")
endif()

set(ESP_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
find_package(Threads REQUIRED)

# The part of src/ that has no Windows dependency (Paths.h falls back to the current directory off Windows).
add_library(esp_core STATIC
  ${ESP_SRC}/Json.cpp ${ESP_SRC}/Presets.cpp ${ESP_SRC}/EspConfig.cpp ${ESP_SRC}/FriendList.cpp
  ${ESP_SRC}/ProfileManager.cpp ${ESP_SRC}/Wire.cpp ${ESP_SRC}/Logger.cpp ${ESP_SRC}/EspRec.cpp
  ${ESP_SRC}/StreamIngest.cpp ${ESP_SRC}/Recorder.cpp ${ESP_SRC}/Replay.cpp ${ESP_SRC}/Token.cpp ${ESP_SRC}/Diagnostics.cpp ${ESP_SRC}/DiagnosticsApp.cpp ${ESP_SRC}/SysInfo.cpp ${ESP_SRC}/Clipboard.cpp)
target_include_directories(esp_core PUBLIC ${ESP_SRC})
target_link_libraries(esp_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(esp_core PUBLIC bcrypt) # Token.cpp: BCryptGenRandom
endif()
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
esp_add_test(esprec_test esprec_test.cpp)
esp_add_test(ingest_test ingest_test.cpp)
esp_add_test(recorder_test recorder_test.cpp)
esp_add_test(replay_test replay_test.cpp)
esp_add_test(wire_validation_test wire_validation_test.cpp)
esp_add_test(wire_version_test wire_version_test.cpp)
esp_add_test(clipboard_test clipboard_test.cpp)
esp_add_test(diagnostics_test diagnostics_test.cpp)
esp_add_test(logger_test logger_test.cpp)
esp_add_test(stress_test stress_test.cpp)
target_compile_definitions(stress_test PRIVATE ESP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
esp_add_test(adapter_choice_test adapter_choice_test.cpp)
esp_add_test(window_state_test window_state_test.cpp)
esp_add_test(dpi_format_test dpi_format_test.cpp)
esp_add_test(gpu_recovery_test gpu_recovery_test.cpp)
esp_add_test(inject_policy_test inject_policy_test.cpp)
esp_add_test(procselect_test procselect_test.cpp)
esp_add_test(selftest_test selftest_test.cpp)
target_compile_definitions(selftest_test PRIVATE ESP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
esp_add_test(seq_test seq_test.cpp)
target_compile_definitions(seq_test PRIVATE ESP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
esp_add_test(token_test token_test.cpp)
target_compile_definitions(token_test PRIVATE ESP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
esp_add_test(wire_fuzz_test wire_fuzz_test.cpp)
target_compile_definitions(wire_fuzz_test PRIVATE ESP_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
if(ESP_FUZZ_ITERS_ENV)
  set_tests_properties(wire_fuzz_test PROPERTIES ENVIRONMENT "${ESP_FUZZ_ITERS_ENV}")
endif()

# ---- golden tests: replay a recording through parser + smoother + camera on a fake clock and compare the pixel jitter ----
add_library(esp_golden STATIC golden.cpp)
target_link_libraries(esp_golden PUBLIC esp_core)
esp_add_test(golden_selftest golden_selftest.cpp)
target_link_libraries(golden_selftest PRIVATE esp_golden)
add_executable(golden_runner golden_runner.cpp)
target_link_libraries(golden_runner PRIVATE esp_golden)
add_executable(golden_sensitivity golden_sensitivity.cpp)
target_link_libraries(golden_sensitivity PRIVATE esp_golden)
# one ctest entry per tests/data/<name>.esprec that has a <name>.golden.json next to it
file(GLOB ESP_RECORDINGS ${CMAKE_CURRENT_SOURCE_DIR}/data/*.esprec)
foreach(rec IN LISTS ESP_RECORDINGS)
  get_filename_component(base ${rec} NAME_WE)
  set(gold ${CMAKE_CURRENT_SOURCE_DIR}/data/${base}.golden.json)
  add_test(NAME golden_${base}
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:golden_runner> -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/run/golden_${base}
            "-DARGS=${rec}|${gold}" -P ${CMAKE_CURRENT_SOURCE_DIR}/run_clean.cmake)
  # ...and proof that it can fail: the same recording without motion smoothing must be rejected
  add_test(NAME golden_sensitivity_${base}
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:golden_sensitivity> -DWORKDIR=${CMAKE_CURRENT_BINARY_DIR}/run/golden_sensitivity_${base}
            "-DARGS=${rec}|${gold}" -P ${CMAKE_CURRENT_SOURCE_DIR}/run_clean.cmake)
endforeach()

# gen_recording writes the synthetic recording(s) under tests/data (see tests/regen-recordings.sh); not run by ctest.
add_executable(gen_recording tools/gen_recording.cpp)
target_link_libraries(gen_recording PRIVATE esp_core)

# ---- are the sanitizers live? Each deliberate bug must be stopped; WILL_FAIL turns "stopped" into a pass and "ran to the end" into a failure ----
if(ESP_SANITIZE)
  add_executable(sanitizer_selfcheck sanitizer_selfcheck.cpp)
  target_link_libraries(sanitizer_selfcheck PRIVATE Threads::Threads)
  if(ESP_SANITIZE MATCHES "thread")
    add_test(NAME selfcheck_tsan_catches_a_race COMMAND sanitizer_selfcheck race)
    set_tests_properties(selfcheck_tsan_catches_a_race PROPERTIES WILL_FAIL TRUE)
  endif()
  if(ESP_SANITIZE MATCHES "address")
    add_test(NAME selfcheck_asan_catches_an_overflow COMMAND sanitizer_selfcheck heap)
    set_tests_properties(selfcheck_asan_catches_an_overflow PROPERTIES WILL_FAIL TRUE)
  endif()
  if(ESP_SANITIZE MATCHES "undefined")
    add_test(NAME selfcheck_ubsan_catches_nan_to_int COMMAND sanitizer_selfcheck ub)
    set_tests_properties(selfcheck_ubsan_catches_nan_to_int PROPERTIES WILL_FAIL TRUE)
  endif()
endif()
