cmake_minimum_required(VERSION 3.5.1)

project(benchmark-datetime CXX)

set(TAG v0.97.3)
set(FULLTAG "tags/${TAG}")


################
## Core Build ##
################

add_custom_target(
  core-build
  cd ../.. && ./util/build-core.sh "${FULLTAG}"
)

include_directories("../core-builds/${FULLTAG}/include")
link_directories("../core-builds/${FULLTAG}/lib")


############
## Celero ##
############

include(ExternalProject)
find_package(Git REQUIRED)

ExternalProject_Add(
  celero
  PREFIX ${CMAKE_BINARY_DIR}/celero
  GIT_REPOSITORY https://github.com/oleks/celero.git
  TIMEOUT 10
  UPDATE_COMMAND ${GIT_EXECUTABLE} pull
  CMAKE_ARGS -DCELERO_ENABLE_EXPERIMENTS=OFF
    -DCELERO_COMPILE_DYNAMIC_LIBRARIES=OFF
  INSTALL_COMMAND ""
)

# Expose required variables to parent scope
ExternalProject_Get_Property(celero install_dir)
set(CELERO_LIB_DIR "${install_dir}/src/celero-build")
set(CELERO_LIBRARIES
    "${CELERO_LIB_DIR}/${CMAKE_SHARED_MODULE_PREFIX}celero${CMAKE_STATIC_LIBRARY_SUFFIX}"
    CACHE INTERNAL "Celero libraries")

ExternalProject_Get_Property(celero source_dir)
set(CELERO_INCLUDE_DIR ${source_dir}/include
    CACHE INTERNAL "Path to include folder for Celero")

include_directories(${CELERO_INCLUDE_DIR})


################
## Executable ##
################

add_definitions(-Werror -Wextra -Wall -pedantic -std=c++11)
add_executable(bench.bin main.cpp)

add_dependencies(bench.bin core-build)
target_link_libraries(bench.bin realm)

add_dependencies(bench.bin celero)
target_link_libraries(bench.bin ${CELERO_LIBRARIES})
