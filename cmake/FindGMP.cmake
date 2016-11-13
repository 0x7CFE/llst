# Try to find the GNU Multiple Precision Arithmetic Library (GMP)
# See http://gmplib.org/

include(FindPackageHandleStandardArgs)

find_path(GMP_INCLUDE_DIRS NAMES gmp.h gmpxx.h)
find_library(GMP_LIBRARIES NAMES gmp gmpxx)

FIND_PACKAGE_HANDLE_STANDARD_ARGS(GMP
    FOUND_VAR GMP_FOUND
    REQUIRED_VARS
        GMP_LIBRARIES
        GMP_INCLUDE_DIRS
)
