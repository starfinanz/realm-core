cmake_minimum_required(VERSION 3.5.1)

project(benchmark-datetime CXX)

set(TAG v0.97.3)
set(FULLTAG "tags/${TAG}")

add_definitions(-Werror -Wextra -Wall -pedantic -std=c++11)
add_executable(bench.bin main.cpp)

include_directories("../core-builds/${FULLTAG}/include")
link_directories("../core-builds/${FULLTAG}/lib")
target_link_libraries(bench.bin realm)

add_custom_target(
  core-build
  cd ../.. && ./util/build-core.sh "${FULLTAG}"
)
add_dependencies(bench.bin core-build)

include(ExternalProject)
find_package(Git REQUIRED)

ExternalProject_Add(
  celero
  GIT_REPOSITORY https://github.com/oleks/Celero.git
  TIMEOUT 10
  UPDATE_COMMAND ${GIT_EXECUTABLE} pull
  INSTALL_COMMAND ""
  LOG_DOWNLOAD ON
)
ExternalProject_Get_Property(celero source_dir)
set(celero_include_dir "${source_dir}/include")
set(celero_src_dir "${source_dir}/src")

include_directories(${celero_include_dir})
link_directories(${celero_src_dir})
