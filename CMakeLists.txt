cmake_minimum_required(VERSION 3.28)
project(CMakeSFMLProject LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include(FetchContent)
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.2
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(SFML)

add_executable(Sketchbook src/main.cpp src/image.hpp)
target_compile_features(Sketchbook PRIVATE cxx_std_17)
target_include_directories(Sketchbook PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(Sketchbook PRIVATE 
    SFML::Graphics
    pdh.lib 
    ws2_32.lib
    ${CMAKE_SOURCE_DIR}/libs/lhwm-cpp-wrapper.lib)

set_target_properties(Sketchbook PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

