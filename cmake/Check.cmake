get_filename_component(
  REPOSITORY_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.."
  ABSOLUTE
)
set(BUILD_DIRECTORY "${REPOSITORY_ROOT}/out/check")

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
