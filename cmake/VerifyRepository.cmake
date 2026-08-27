find_package(Git REQUIRED)

get_filename_component(
  REPOSITORY_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.."
  ABSOLUTE
)

function(check_git_diff)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" diff ${ARGN} --check
    WORKING_DIRECTORY "${REPOSITORY_ROOT}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
  )
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${output}${error}")
  endif()
endfunction()

function(verify_fixture file_name expected_hash)
  set(
    fixture_path
    "${REPOSITORY_ROOT}/neko_tests/vpu/integration/${file_name}"
  )
  if(NOT EXISTS "${fixture_path}")
    message(FATAL_ERROR "Missing integration fixture: ${file_name}")
  endif()

  file(SHA256 "${fixture_path}" actual_hash)
  if(NOT actual_hash STREQUAL expected_hash)
    message(
      FATAL_ERROR
      "${file_name} SHA-256 mismatch\n"
      "  expected: ${expected_hash}\n"
      "  actual:   ${actual_hash}"
    )
  endif()
endfunction()

check_git_diff()
check_git_diff(--cached)

verify_fixture(
  integer_fill.bin
  45d00e599bdcfe54128bc58cba4d565c851bb523e0ba3a637c1a6782550fae52
)
verify_fixture(
  common_integer.bin
  4c5f247ab39053142ca9be9652caffd98d38a5417e8655856fea1d52073fb46d
)
verify_fixture(
  memory_variants.bin
  d0be0bcc8129c3e592282807c242468b9daa5afda9a919d7e31edf84ea0ad786
)
verify_fixture(
  lane_masks.bin
  41e29c6379fbbd55747539b13b467cb8e6248b45ae712de9968965a471b9c23a
)
verify_fixture(
  branch_paths.bin
  ee4643e517cb599fc0d654e74bd126bf0e980434ac545333b81ce6f34b992c45
)
verify_fixture(
  branch_family.bin
  bada7ef8bea9dc2e3d2a363b2906f64ad5d539cc0a3afa9780d6510de6aa1964
)
verify_fixture(
  indirect_calls.bin
  dbde6ab0815c053b766f3aa57a84fe47c45ec89a20c32833c05a9fad130828df
)
verify_fixture(
  vector_math.bin
  2cf3f043920d723f82d3b8157abe4f6909e3e4566baff3d533c2a491a97b28d0
)
verify_fixture(
  dual_issue.bin
  70acf8379442ac7f545d3273bbb5c34cdcf9cdceff74aed87efc3dd2b9cb5bb9
)
verify_fixture(
  termination.bin
  3618324b61cedc436579ec6a9de2f29be3440185fc2cbdf99e780c4bfdfbbb21
)
verify_fixture(
  vector_kernel.bin
  e5294f1866952531f320fa7306a4a772a80500bb6020d18f822656da295cb971
)
verify_fixture(
  pipeline_acc_overlap.bin
  e1eca2dab10db0aa967f4e2c8044e0dd3508acfd9ac688838f852ff2ff300a27
)
verify_fixture(
  pipeline_integer_control.bin
  7321275edc8c4cfd331d7350f06b0e0ad2c781d783534b5f849ce65be4a5b216
)
verify_fixture(
  pipeline_loi_timing.bin
  01a78ff81e6dea8e2d8993c87d7105d002a085235c6d67082add9a3fa7223227
)
verify_fixture(
  pipeline_termination_drain.bin
  f0b165309aaab04653ba8dbedd9a3d8360500eb46d3e130342bbf7ea2d949466
)
verify_fixture(
  floating_point_truncation.bin
  b5e0ab57cda49059f30205cd4cd001aeb55dff820424e898f6031304ecb59940
)
verify_fixture(
  floating_point_exceptions.bin
  dad420fa5f4c134fbfe12308be2a79dbadfc9e72b810754c97cde7b8a27d79c2
)
verify_fixture(
  floating_point_compound.bin
  d8fdaf73e48383eb5a857ce6159420871591ed884ae9d78971f4d5059abab2f4
)
verify_fixture(
  fixed_point_conversions.bin
  3c1594daf9c09d02701979295ccee63c7c06caa722bf6ca88410ab403261ec6f
)
verify_fixture(
  q_pipeline.bin
  6421afcad9ae775630c93af16320369c6984dae51286ca89027ee404bf2d73d6
)
verify_fixture(
  xgkick.bin
  af1e1b8aa5346bd5d772d027601d376172fab436fadf088cd20e51f61201d628
)
verify_fixture(
  register_movement.bin
  b6883b705a935dfd8461cf7317211eced1a9fc61ef076d72cf033bd34a7c1fbc
)
verify_fixture(
  clipping_flags.bin
  4831d97f4321d506997ded681c5e5cf176444397836e7602efbfd104f202eccb
)
verify_fixture(
  mac_flags.bin
  35c3bb50455eb2f96fc17cd6bc3be09e4788730988d2bd2f53c79b498218fea0
)

message(STATUS "Repository checks passed")
