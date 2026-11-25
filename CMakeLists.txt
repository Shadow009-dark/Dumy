# ------------------------------------------------------------------
# DUMY - Local AI agriculture chatbot
# C++17 / CMake / Ollama / RAG over PDFs + web
#
# Dependencies:
#   - libcurl          (HTTP client: Ollama, web search, page downloads)
#   - nlohmann-json    (JSON)   - found or fetched automatically
#   - cpp-httplib      (server) - found or fetched automatically
#   - poppler-cpp      (PDF text extraction) - optional, auto-disabled
#
# Windows (Visual Studio + vcpkg manifest): configure with
#   -DCMAKE_TOOLCHAIN_FILE="<VS>/VC/vcpkg/scripts/buildsystems/vcpkg.cmake"
# Ubuntu: sudo apt install cmake g++ libcurl4-openssl-dev nlohmann-json3-dev
#         libcpp-httplib-dev libpoppler-cpp-dev
# ------------------------------------------------------------------
cmake_minimum_required(VERSION 3.16)
project(DUMY VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(DUMY_WITH_POPPLER "Enable PDF support (needs poppler-cpp)" ON)
option(DUMY_FETCH_MISSING "Download nlohmann-json / cpp-httplib when not installed" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---------------- Threads ----------------
find_package(Threads REQUIRED)

# ---------------- libcurl ----------------
find_package(CURL CONFIG QUIET)
if(NOT CURL_FOUND)
  find_package(CURL REQUIRED)
endif()
if(TARGET CURL::libcurl)
  set(DUMY_CURL_LIB CURL::libcurl)
else()
  add_library(dumy_curl INTERFACE)
  target_include_directories(dumy_curl INTERFACE ${CURL_INCLUDE_DIRS})
  target_link_libraries(dumy_curl INTERFACE ${CURL_LIBRARIES})
  set(DUMY_CURL_LIB dumy_curl)
endif()

# ---------------- nlohmann/json ----------------
find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
  set(DUMY_JSON_LIB nlohmann_json::nlohmann_json)
elseif(DUMY_FETCH_MISSING)
  message(STATUS "nlohmann-json not found locally - fetching from GitHub")
  include(FetchContent)
  FetchContent_Declare(nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
  FetchContent_MakeAvailable(nlohmann_json)
  set(DUMY_JSON_LIB nlohmann_json::nlohmann_json)
else()
  message(FATAL_ERROR "nlohmann-json not found. Install it (apt/vcpkg) or set DUMY_FETCH_MISSING=ON")
endif()

# ---------------- cpp-httplib ----------------
find_package(httplib CONFIG QUIET)
if(httplib_FOUND)
  set(DUMY_HTTPLIB_LIB httplib::httplib)
elseif(DUMY_FETCH_MISSING)
  message(STATUS "cpp-httplib not found locally - fetching from GitHub")
  include(FetchContent)
  FetchContent_Declare(httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.1.tar.gz)
  set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(httplib)
  set(DUMY_HTTPLIB_LIB httplib::httplib)
else()
  message(FATAL_ERROR "cpp-httplib not found. Install it (apt/vcpkg) or set DUMY_FETCH_MISSING=ON")
endif()

# ---------------- poppler-cpp (optional, for PDF text extraction) ----------------
set(DUMY_HAS_POPPLER 0)
if(DUMY_WITH_POPPLER)
  find_package(PopplerCpp QUIET)
  if(PopplerCpp_FOUND)
    set(DUMY_HAS_POPPLER 1)
    message(STATUS "poppler-cpp found - PDF support ENABLED")
  else()
    message(WARNING "poppler-cpp NOT found - PDF support DISABLED. "
                    "Install: Ubuntu 'sudo apt install libpoppler-cpp-dev' / Windows vcpkg 'poppler'")
  endif()
endif()

# ---------------- The executable ----------------
set(DUMY_SOURCES
  src/main.cpp
  src/config.cpp
  src/chunker.cpp
  src/pdf_loader.cpp
  src/ollama_client.cpp
  src/vector_store.cpp
  src/bm25_index.cpp
  src/html_cleaner.cpp
  src/web_search.cpp
  src/retriever.cpp
  src/reranker.cpp
  src/indexer.cpp
  src/rag_pipeline.cpp
  src/session_manager.cpp
  src/http_server.cpp
  src/key_manager.cpp
  src/api_keys_api.cpp
  src/weather_tool.cpp
  src/conversation_store.cpp
)

add_executable(dumy ${DUMY_SOURCES})
target_include_directories(dumy PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(dumy PRIVATE DUMY_HAS_POPPLER=${DUMY_HAS_POPPLER})
target_link_libraries(dumy PRIVATE
  Threads::Threads
  ${DUMY_CURL_LIB}
  ${DUMY_JSON_LIB}
  ${DUMY_HTTPLIB_LIB}
)
if(DUMY_HAS_POPPLER)
  target_link_libraries(dumy PRIVATE PopplerCpp::PopplerCpp)
endif()

if(MSVC)
  # Source files and string literals are UTF-8 (Azerbaijani text!)
  target_compile_options(dumy PRIVATE /utf-8 /W3 /permissive-)
  target_compile_definitions(dumy PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
else()
  target_compile_options(dumy PRIVATE -Wall -Wextra)
endif()

# ---------------- Unit tests (header-only, no server, no model) --------
add_executable(dumy_unit_tests tests/test_quality_gate.cpp)
target_include_directories(dumy_unit_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(dumy_unit_tests PRIVATE ${DUMY_JSON_LIB})
if(MSVC)
  target_compile_options(dumy_unit_tests PRIVATE /utf-8 /W3 /permissive-)
  target_compile_definitions(dumy_unit_tests PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
endif()

# Copy vcpkg runtime DLLs next to the .exe (Windows, dynamic triplet)
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR)
  file(GLOB _DUMY_VCPKG_DLLS "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/*.dll")
  if(_DUMY_VCPKG_DLLS)
    add_custom_command(TARGET dumy POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_DUMY_VCPKG_DLLS} $<TARGET_FILE_DIR:dumy>
      COMMENT "Copying vcpkg runtime DLLs next to dumy.exe")
  endif()
endif()

# ---------------- Weather tool tests (no server, no model, no network) --
add_executable(dumy_weather_tests
  tests/test_weather_tool.cpp
  src/weather_tool.cpp
  src/key_manager.cpp)
target_include_directories(dumy_weather_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(dumy_weather_tests PRIVATE
  Threads::Threads
  ${DUMY_CURL_LIB}
  ${DUMY_JSON_LIB})
if(MSVC)
  target_compile_options(dumy_weather_tests PRIVATE /utf-8 /W3 /permissive-)
  target_compile_definitions(dumy_weather_tests PRIVATE
    _CRT_SECURE_NO_WARNINGS
    NOMINMAX
    WIN32_LEAN_AND_MEAN
  )
endif()

install(TARGETS dumy RUNTIME DESTINATION bin)
