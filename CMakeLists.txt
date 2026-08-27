cmake_minimum_required(VERSION 3.20)

project(neko LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(
    NEKO_ENABLE_SANITIZERS
    "Build Neko with AddressSanitizer"
    OFF
)

add_library(neko_sanitizers INTERFACE)
if(NEKO_ENABLE_SANITIZERS)
    if(MSVC)
        target_compile_options(neko_sanitizers INTERFACE /fsanitize=address)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(
            neko_sanitizers
            INTERFACE
                -fsanitize=address
                -fno-omit-frame-pointer
        )
        target_link_options(neko_sanitizers INTERFACE -fsanitize=address)
    else()
        message(
            FATAL_ERROR
            "NEKO_ENABLE_SANITIZERS is unsupported by this compiler"
        )
    endif()
endif()

add_library(neko_core
    neko/clock/clock_scheduler.cpp
    neko/fp_register.cpp
    neko/ee/vpu/vpu.cpp
    neko/ee/vpu/vpu_lower_instruction.cpp
    neko/ee/vpu/pipelines/vpu_pipeline.cpp
    neko/ee/vpu/pipelines/vpu_pipeline_orchestrator.cpp
    neko/math/float.cpp
    neko/math/floating_point_ops.cpp
)
target_link_libraries(neko_core PUBLIC neko_sanitizers)

target_include_directories(neko_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/neko
        ${CMAKE_CURRENT_SOURCE_DIR}/neko/clock
        ${CMAKE_CURRENT_SOURCE_DIR}/neko/ee/vpu
        ${CMAKE_CURRENT_SOURCE_DIR}/neko/ee/vpu/pipelines
        ${CMAKE_CURRENT_SOURCE_DIR}/neko/math
)

add_executable(neko neko/main.cpp)
target_link_libraries(neko PRIVATE neko_core)

add_executable(neko_perf
    neko_perf/main.cpp
    neko_perf/clock/stop_watch.cpp
)
target_include_directories(neko_perf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/neko_perf/clock)
target_link_libraries(neko_perf PRIVATE neko_core)

add_library(neko_diagnostics
    neko_diagnostics/vpu_program_runner.cpp
)
target_include_directories(neko_diagnostics
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/neko_diagnostics
)
target_link_libraries(neko_diagnostics PUBLIC neko_core)

add_executable(neko_tests
    neko_tests/main.cpp
    neko_tests/fp_register_tests.cpp
    neko_tests/math/floating_point_tests.cpp
    neko_tests/vpu/vpu_flag_tests.cpp
    neko_tests/vpu/vpu_fp_calculation_tests.cpp
    neko_tests/vpu/integration/branch_paths_tests.cpp
    neko_tests/vpu/integration/common_integer_tests.cpp
    neko_tests/vpu/integration/dual_issue_tests.cpp
    neko_tests/vpu/integration/floating_point_integration_tests.cpp
    neko_tests/vpu/integration/indirect_calls_tests.cpp
    neko_tests/vpu/integration/integer_fill_tests.cpp
    neko_tests/vpu/integration/lane_masks_tests.cpp
    neko_tests/vpu/integration/memory_variants_tests.cpp
    neko_tests/vpu/integration/pipeline_integration_tests.cpp
    neko_tests/vpu/integration/termination_tests.cpp
    neko_tests/vpu/integration/vector_math_tests.cpp
    neko_tests/vpu/integration/vector_kernel_tests.cpp
    neko_tests/vpu/integration/vpu_integration_test_utils.cpp
    neko_tests/vpu/vpu_debug_tests.cpp
    neko_tests/vpu/vpu_floating_point_diagnostics_tests.cpp
    neko_tests/vpu/vpu_memory_tests.cpp
    neko_tests/vpu/vpu_microinstruction_tests.cpp
    neko_tests/vpu/vpu_lower_instruction_tests.cpp
    neko_tests/vpu/vpu_lower_timing_conformance_tests.cpp
    neko_tests/vpu/vpu_pipeline_tests.cpp
    neko_tests/vpu/vpu_program_runner_tests.cpp
    neko_tests/vpu/vpu_state_tests.cpp
    neko_tests/vpu/vpu_timing_conformance_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_add_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_clip_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_fixed_point_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_instruction_stall_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_instruction_utils.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_madd_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_max_min_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_msub_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_mul_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_sub_tests.cpp
    neko_tests/vpu/opcode_tests/vpu_upper_vector_outer_product_tests.cpp
)
target_include_directories(neko_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/neko_tests
    ${CMAKE_CURRENT_SOURCE_DIR}/neko_tests/vpu/opcode_tests
)
target_compile_definitions(neko_tests PRIVATE
    NEKO_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/neko_tests/vpu/integration"
)
target_link_libraries(neko_tests PRIVATE neko_diagnostics)

enable_testing()
add_test(NAME neko_tests COMMAND neko_tests)

add_custom_target(check
    COMMAND $<TARGET_FILE:neko_tests>
    COMMAND
        ${CMAKE_COMMAND}
        -P
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifyRepository.cmake
    DEPENDS neko_tests
    USES_TERMINAL
)
