# Blockworlds (BW) mod integration.
#
# Every file the mod owns lives under src/blockworlds/, so this glob is the only
# place the build has to know about it: adding or removing BW sources never
# requires touching upstream's CMakeLists.txt again.
#
# Included from CMakeLists.txt right after SERVER_SRC is assembled, and appends
# to it in the same scope.

file(GLOB_RECURSE BLOCKWORLDS_SRC
  "${PROJECT_SOURCE_DIR}/src/blockworlds/*.cpp"
  "${PROJECT_SOURCE_DIR}/src/blockworlds/*.h"
  "${PROJECT_SOURCE_DIR}/src/blockworlds/*.hpp"
)
# Unit tests are compiled into testrunner, not into the server.
list(FILTER BLOCKWORLDS_SRC EXCLUDE REGEX "/src/blockworlds/tests/")

list(APPEND SERVER_SRC ${BLOCKWORLDS_SRC})

# BW's Log()/SendChatTarget() helpers are variadic templates that forward a
# runtime format string into dbg_msg/str_format. GCC checks that at the point of
# instantiation, so the usual in-header pragma does not travel with the
# template; every caller passes a literal. Scoped to BW sources only -- upstream
# files keep the project's warning settings untouched.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(${BLOCKWORLDS_SRC} PROPERTIES
    COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security"
  )
endif()
