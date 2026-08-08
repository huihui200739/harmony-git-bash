cmake_minimum_required(VERSION 3.5.0)
project(harmony_git_bash)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
  git_native
  SHARED
  git_repository.cpp
  napi_init.cpp
)

target_include_directories(
  git_native
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(
  git_native
  PUBLIC
  libace_napi.z.so
)
