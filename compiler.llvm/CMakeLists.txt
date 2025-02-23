cmake_minimum_required(VERSION 3.15)

# Project name
project(tapir2vitis)

if(NOT OPENCILK_HOME)
    set(OPENCILK_HOME "/opt/OpenCilk")
endif()

# Set the CMake prefix path to include custom LLVM and Clang installation paths
set(CMAKE_PREFIX_PATH "${OPENCILK_HOME}/lib/cmake")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -fsanitize=address")

# Specify the custom LLVM installation path
set(LLVM_DIR "/opt/OpenCilk/lib/cmake/llvm")

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

# Add LLVM headers
include_directories(${LLVM_INCLUDE_DIRS})
include_directories("${OPENCILK_HOME}/include")
# Link directories
link_directories("${OPENCILK_HOME}/lib")

# Add LLVM libraries
add_definitions(${LLVM_DEFINITIONS})

# Export compile commands for Clang tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source file
set(SOURCE_FILES
    src/main.cpp
)

# Add executable
add_executable(tapir2vitis ${SOURCE_FILES})

set_target_properties(tapir2vitis PROPERTIES
    # CXX_COMPILER /opt/OpenCilk/bin/clang++
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Link against LLVM libraries
target_link_libraries(tapir2vitis PRIVATE
    LLVMCore
    LLVMIRReader
    LLVMTransformUtils
)
