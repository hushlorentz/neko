find_package(Python3 REQUIRED COMPONENTS Interpreter)

get_filename_component(
  REPOSITORY_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.."
  ABSOLUTE
)

if(NOT DEFINED NEKO_TEST_BINARY OR NOT EXISTS "${NEKO_TEST_BINARY}")
  message(FATAL_ERROR "NEKO_TEST_BINARY must name the built headless executable.")
endif()

execute_process(
  COMMAND
    "${CMAKE_COMMAND}" -E env "NEKO_TEST_BINARY=${NEKO_TEST_BINARY}"
    "${Python3_EXECUTABLE}" -B -m unittest discover -s tools -p "test_*.py"
  WORKING_DIRECTORY "${REPOSITORY_ROOT}"
  RESULT_VARIABLE result
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "Python tooling/integration tests failed (${result}).")
endif()
