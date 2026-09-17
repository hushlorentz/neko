if(NOT APPLE)
  message(FATAL_ERROR "The full leak check requires macOS /usr/bin/leaks.")
endif()

get_filename_component(
  REPOSITORY_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.."
  ABSOLUTE
)
set(BUILD_DIRECTORY "${REPOSITORY_ROOT}/out/leaks")

function(run_checked)
  execute_process(
    COMMAND ${ARGN}
    WORKING_DIRECTORY "${REPOSITORY_ROOT}"
    RESULT_VARIABLE result
  )
  if(NOT result EQUAL 0)
    string(JOIN " " command ${ARGN})
    message(FATAL_ERROR "Command failed (${result}): ${command}")
  endif()
endfunction()

run_checked(
  "${CMAKE_COMMAND}"
  -S
  "${REPOSITORY_ROOT}"
  -B
  "${BUILD_DIRECTORY}"
  -D
  CMAKE_BUILD_TYPE=Debug
  -D
  NEKO_ENABLE_SANITIZERS=OFF
  -D
  NEKO_OPTIMIZE_CHECKS=ON
)
run_checked(
  "${CMAKE_COMMAND}"
  --build
  "${BUILD_DIRECTORY}"
  --target
  neko_tests
  --config
  Debug
)

set(TEST_EXECUTABLE "${BUILD_DIRECTORY}/neko_tests")
if(NOT EXISTS "${TEST_EXECUTABLE}")
  set(TEST_EXECUTABLE "${BUILD_DIRECTORY}/Debug/neko_tests")
endif()
if(NOT EXISTS "${TEST_EXECUTABLE}")
  message(FATAL_ERROR "Could not find the leak-check test executable")
endif()

run_checked(
  "${CMAKE_COMMAND}"
  -E
  env
  MallocStackLogging=1
  /usr/bin/leaks
  --atExit
  --
  "${TEST_EXECUTABLE}"
)
