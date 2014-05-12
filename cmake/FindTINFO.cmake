
# Find libtinfo (developer's library for the low-level terminfo library)
# Export variables:
#  TINFO_FOUND
#  TINFO_INCLUDE_DIRS
#  TINFO_LIBRARIES

find_library(TINFO_LIBRARIES
    NAMES tinfo
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS( tinfo DEFAULT_MSG TINFO_LIBRARIES )
