
# Find libreadline (terminal input library)
# Export variables:
#  READLINE_FOUND
#  READLINE_INCLUDE_DIRS
#  READLINE_LIBRARIES

find_path(READLINE_INCLUDE_DIRS
    NAMES readline/readline.h
)

find_library(READLINE_LIBRARIES
    NAMES readline
)

include(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(readline DEFAULT_MSG READLINE_LIBRARIES READLINE_INCLUDE_DIRS )
