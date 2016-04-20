cmake_minimum_required(VERSION 3.5.1)

set(TAG v0.97.3)
set(FULLTAG "tags/${TAG}")

project(benchmark-datetime)

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
