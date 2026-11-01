# FindPopplerCpp.cmake
# Locates poppler-cpp (the C++ binding of the poppler PDF library).
# Defines: PopplerCpp_FOUND, PopplerCpp::PopplerCpp
#
# Works with:
#   - pkg-config based systems (Ubuntu/Debian: libpoppler-cpp-dev)
#   - vcpkg installed trees (include/poppler/cpp/poppler.h + lib/poppler-cpp.lib)

find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_POPPLER_CPP QUIET poppler-cpp)
endif()

find_path(POPPLER_CPP_INCLUDE_DIR
  NAMES poppler/cpp/poppler-document.h poppler/cpp/poppler-page.h
  HINTS ${PC_POPPLER_CPP_INCLUDE_DIRS}
  PATH_SUFFIXES include
)
find_library(POPPLER_CPP_LIBRARY
  NAMES poppler-cpp poppler-cpp-1
  HINTS ${PC_POPPLER_CPP_LIBRARY_DIRS}
  PATH_SUFFIXES lib
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(PopplerCpp
  REQUIRED_VARS POPPLER_CPP_LIBRARY POPPLER_CPP_INCLUDE_DIR
)

if(PopplerCpp_FOUND AND NOT TARGET PopplerCpp::PopplerCpp)
  add_library(PopplerCpp::PopplerCpp UNKNOWN IMPORTED)
  set_target_properties(PopplerCpp::PopplerCpp PROPERTIES
    IMPORTED_LOCATION "${POPPLER_CPP_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${POPPLER_CPP_INCLUDE_DIR}"
  )
endif()

mark_as_advanced(POPPLER_CPP_INCLUDE_DIR POPPLER_CPP_LIBRARY)
