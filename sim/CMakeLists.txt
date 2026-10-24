# Offline simulator for benchmarking strategy variants. Separate from the
# official bot build so the starter CMakeLists.txt stays untouched.
#   cmake -S sim -B sim/build && cmake --build sim/build --config Release
cmake_minimum_required(VERSION 3.20)
project(lpc_sim LANGUAGES CXX)
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    URL_HASH SHA256=42f6e95cad6ec532fd372391373363b62a14af6d771056dbfc86160e6dfff7aa)
  FetchContent_MakeAvailable(json)
endif()
add_executable(lpc-sim sim.cpp)
target_include_directories(lpc-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(lpc-sim PRIVATE nlohmann_json::nlohmann_json)
set_target_properties(lpc-sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
