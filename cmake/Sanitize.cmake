get_filename_component(
  REPOSITORY_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.."
  ABSOLUTE
)
set(BUILD_DIRECTORY "${REPOSITORY_ROOT}/out/sanitize")

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
  NEKO_ENABLE_SANITIZERS=ON
)
run_checked(
  "${CMAKE_COMMAND}"
  --build
  "${BUILD_DIRECTORY}"
  --target
  check
  --config
  Debug
)

if(APPLE)
  set(LEAK_BUILD_DIRECTORY "${REPOSITORY_ROOT}/out/leaks")
  run_checked(
    "${CMAKE_COMMAND}"
    -S
    "${REPOSITORY_ROOT}"
    -B
    "${LEAK_BUILD_DIRECTORY}"
    -D
    CMAKE_BUILD_TYPE=Debug
    -D
    NEKO_ENABLE_SANITIZERS=OFF
  )
  run_checked(
    "${CMAKE_COMMAND}"
    --build
    "${LEAK_BUILD_DIRECTORY}"
    --target
    neko_tests
    --config
    Debug
  )

  set(TEST_EXECUTABLE "${LEAK_BUILD_DIRECTORY}/neko_tests")
  if(NOT EXISTS "${TEST_EXECUTABLE}")
    set(TEST_EXECUTABLE "${LEAK_BUILD_DIRECTORY}/Debug/neko_tests")
  endif()
  if(NOT EXISTS "${TEST_EXECUTABLE}")
    message(FATAL_ERROR "Could not find the sanitizer test executable")
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
endif()
