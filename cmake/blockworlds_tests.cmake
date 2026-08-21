# Blockworlds unit tests, appended to the testrunner target.
#
# Included from CMakeLists.txt right before the testrunner is created, in the
# scope where TESTS exists. The testrunner already links
# game-server-without-main, which contains every BW source, so only the test
# files themselves have to be added here.

file(GLOB BLOCKWORLDS_TESTS "${PROJECT_SOURCE_DIR}/src/blockworlds/tests/*.cpp")
list(APPEND TESTS ${BLOCKWORLDS_TESTS})

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(${BLOCKWORLDS_TESTS} PROPERTIES
    COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security"
  )
endif()
