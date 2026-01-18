cmake_minimum_required(VERSION 3.28)
project(CMakeSFMLProject LANGUAGES CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "/O2")

include(FetchContent)

set(JPEG_TURBO_DIR "C:/libjpeg-turbo64")

# SFML
FetchContent_Declare(SFML
    GIT_REPOSITORY https://github.com/SFML/SFML.git
    GIT_TAG 3.0.2
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(SFML)

# TinyXML2
FetchContent_Declare(tinyxml2
    GIT_REPOSITORY https://github.com/leethomason/tinyxml2.git
    GIT_TAG 10.0.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(tinyxml2)

# JSON parser
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(json)

# TOML parser
FetchContent_Declare(tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
    GIT_SHALLOW ON
    EXCLUDE_FROM_ALL
    SYSTEM)
FetchContent_MakeAvailable(tomlplusplus)

add_executable(Sketchbook WIN32 src/main.cpp src/image.hpp resources/app.rc)
target_compile_features(Sketchbook PRIVATE cxx_std_20)
target_include_directories(Sketchbook PRIVATE ${JPEG_TURBO_DIR}/include)
target_include_directories(Sketchbook PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(Sketchbook PRIVATE 
    SFML::Graphics
    nlohmann_json::nlohmann_json
    tomlplusplus::tomlplusplus
    pdh.lib 
    ws2_32.lib
    tinyxml2::tinyxml2
    ${JPEG_TURBO_DIR}/lib/turbojpeg-static.lib
)

# set_target_properties(Sketchbook PROPERTIES WIN32_EXECUTABLE TRUE) # Set to false for debugging console output
set_target_properties(Sketchbook PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

add_custom_target(copy_assets
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_LIST_DIR}/resources ${CMAKE_CURRENT_BINARY_DIR}/bin/Release/resources
)
add_dependencies(Sketchbook copy_assets)

