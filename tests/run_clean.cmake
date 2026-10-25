# Runs a test executable in a freshly created directory:  cmake -DEXE=<exe> -DWORKDIR=<dir> [-DARGS=a|b] -P run_clean.cmake
# (arguments are separated by '|' because ';' cannot be passed through -D)
# Used by tests/CMakeLists.txt. The exit code of the test is the exit code of this script.
if(NOT EXE OR NOT WORKDIR)
  message(FATAL_ERROR "run_clean.cmake: EXE and WORKDIR are required")
endif()
string(REPLACE "|" ";" ARGS "${ARGS}")
file(REMOVE_RECURSE "${WORKDIR}")
file(MAKE_DIRECTORY "${WORKDIR}")
get_filename_component(_name "${EXE}" NAME)
get_filename_component(_exedir "${EXE}" DIRECTORY)
file(COPY "${EXE}" DESTINATION "${WORKDIR}")
# MinGW test executables may need runtime DLLs (libwinpthread-1.dll); copy any that sit next to the exe.
file(GLOB _dlls "${_exedir}/*.dll")
foreach(d IN LISTS _dlls)
  file(COPY "${d}" DESTINATION "${WORKDIR}")
endforeach()
execute_process(COMMAND "${WORKDIR}/${_name}" ${ARGS}
                WORKING_DIRECTORY "${WORKDIR}" RESULT_VARIABLE _rc)
if(NOT _rc EQUAL 0)
  message(FATAL_ERROR "${_name} failed (exit code ${_rc})")
endif()
