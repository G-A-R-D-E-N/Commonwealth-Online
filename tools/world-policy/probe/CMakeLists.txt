cmake_minimum_required(VERSION 3.25)
project(co_world_policy_probe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(RADICLE_INCLUDE_DIR "" CACHE PATH "Radicle include directory")
set(RADICLE_FO4_LIBRARY "" CACHE FILEPATH "Radicle fo4 static library")
set(ZLIB_LIBRARY "" CACHE FILEPATH "zlib library Radicle links against")

if(RADICLE_INCLUDE_DIR STREQUAL "" OR RADICLE_FO4_LIBRARY STREQUAL "" OR ZLIB_LIBRARY STREQUAL "")
  message(FATAL_ERROR "set -DRADICLE_INCLUDE_DIR, -DRADICLE_FO4_LIBRARY and -DZLIB_LIBRARY")
endif()

add_executable(probe probe.cpp)
target_include_directories(probe PRIVATE "${RADICLE_INCLUDE_DIR}")
target_compile_options(probe PRIVATE /utf-8 /EHsc)
target_link_libraries(probe PRIVATE "${RADICLE_FO4_LIBRARY}" "${ZLIB_LIBRARY}")
