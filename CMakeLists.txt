cmake_minimum_required(VERSION 3.10)

project(scs-sdk-controller)

set(CMAKE_CXX_STANDARD 17)

add_library(scs_sdk_controller SHARED scs_sdk_controller.cpp)

target_link_options(scs_sdk_controller PRIVATE "-static-libstdc++" "-static-libgcc")

target_include_directories(scs_sdk_controller
                           PRIVATE
                           "${CMAKE_CURRENT_SOURCE_DIR}/scs_sdk/include/"
                           )