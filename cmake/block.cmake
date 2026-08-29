# Block mod integration.
#
# Every file the mod owns lives under src/block/, so this glob is the only
# place the build has to know about it: adding or removing Block sources never
# requires touching upstream's CMakeLists.txt again.
#
# Included from CMakeLists.txt right after SERVER_SRC is assembled, and appends
# to it in the same scope.

# CONFIGURE_DEPENDS so a new source file is picked up by build directories that
# are already configured, instead of only by a fresh CI checkout.
file(GLOB_RECURSE BLOCK_SRC CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/block/*.cpp"
  "${PROJECT_SOURCE_DIR}/src/block/*.h"
  "${PROJECT_SOURCE_DIR}/src/block/*.hpp"
)
# Unit tests are compiled into testrunner, not into the server.
list(FILTER BLOCK_SRC EXCLUDE REGEX "/src/block/tests/")

list(APPEND SERVER_SRC ${BLOCK_SRC})

# Block's Log()/SendChatTarget() helpers are variadic templates that forward a
# runtime format string into dbg_msg/str_format. GCC checks that at the point of
# instantiation, so the usual in-header pragma does not travel with the
# template; every caller passes a literal. Scoped to Block sources only -- upstream
# files keep the project's warning settings untouched.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set_source_files_properties(${BLOCK_SRC} PROPERTIES
    COMPILE_OPTIONS "-Wno-format-nonliteral;-Wno-format-security"
  )
endif()
