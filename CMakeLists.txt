cmake_minimum_required(VERSION 3.30)
project(Cv)

###################################################
#
###################################################
set(PROJECT_ROOT_DIR "${CMAKE_SOURCE_DIR}")

set(PROJECT_BUILD_TYPE Debug)

set(PROJECT_INCLUDE_DIR "${PROJECT_ROOT_DIR}/include/")
set(PROJECT_3RDPARTY_INCLUDE_DIR "${PROJECT_ROOT_DIR}/3rdparty/")
set(PROJECT_LIB_DIR "${PROJECT_ROOT_DIR}/lib")
set(PROJECT_ASSETS_DIR "${PROJECT_ROOT_DIR}/assets")
string(CONCAT PROJECT_CONAN_TOOL_CHAIN
        "${PROJECT_LIB_DIR}/build/"
        "${PROJECT_BUILD_TYPE}"
        "/generators/conan_toolchain.cmake"
)

add_definitions(-DPROJECT_ASSETS_DIR=\"${PROJECT_ASSETS_DIR}\")

###################################################
#
###################################################
set(CMAKE_BUILD_TYPE ${PROJECT_BUILD_TYPE})
set(CMAKE_TOOLCHAIN_FILE "${PROJECT_CONAN_TOOL_CHAIN}" CACHE FILEPATH "Conan toolchain file" FORCE)

set(CMAKE_CXX_STANDARD 17)
###################################################
#
###################################################
include(${PROJECT_CONAN_TOOL_CHAIN})

#find_package(fmt REQUIRED)
#find_package(spdlog REQUIRED)
#find_package(nlohmann_json REQUIRED)
#find_package(yaml-cpp REQUIRED)
find_package(OpenCV REQUIRED CONFIG)


include_directories(include)
include_directories(3rdparty)


include_directories("${PROJECT_INCLUDE_DIR}")
include_directories("${PROJECT_3RDPARTY_INCLUDE_DIR}")
include_directories(${OpenCV_INCLUDE_DIRS})
###################################################
#
###################################################

message(STATUS "Project root directory: ${PROJECT_ROOT_DIR}")
message(STATUS "Project include directory: ${PROJECT_CORE_INCLUDE_DIR}")
message(STATUS "Project library directory: ${PROJECT_LIB_DIR}")
message(STATUS "Project build type: ${PROJECT_BUILD_TYPE}")
message(STATUS "Project conan toolchain file: ${PROJECT_CONAN_TOOL_CHAIN}")

###################################################
#
###################################################

add_executable(Cv main.cpp)

###################################################
#
###################################################

#target_link_libraries(Cv fmt::fmt)
#target_link_libraries(Cv spdlog::spdlog)
#target_link_libraries(Cv yaml-cpp::yaml-cpp)
#target_link_libraries(Cv nlohmann_json::nlohmann_json)
target_link_libraries(Cv ${OpenCV_LIBS})
