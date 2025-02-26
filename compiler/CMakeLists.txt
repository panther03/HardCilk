cmake_minimum_required(VERSION 3.15)

# Project name
project(cilk2vitis)

if(NOT OPENCILK_HOME)
    set(OPENCILK_HOME "/opt/OpenCilk")
endif()

# Set the CMake prefix path to include custom LLVM and Clang installation paths
set(CMAKE_PREFIX_PATH "${OPENCILK_HOME}/lib/cmake")

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -g -fsanitize=address")

# Specify the custom LLVM and Clang installation path
set(LLVM_DIR "${OPENCILK_HOME}/lib/cmake/llvm")
set(CLANG_DIR "${OPENCILK_HOME}/lib/cmake/clang")

# Find LLVM and Clang
find_package(LLVM REQUIRED CONFIG)
find_package(Clang REQUIRED CONFIG)

# Add LLVM headers
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})
include_directories("${OPENCILK_HOME}/include")
# Link directories
link_directories("${OPENCILK_HOME}/lib")

# Add LLVM and Clang libraries
add_definitions(${LLVM_DEFINITIONS})

# Export compile commands for Clang tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source file
set(SOURCE_FILES
    src/main.cpp
)

# Add executable
add_executable(cilk2vitis ${SOURCE_FILES})

set_target_properties(cilk2vitis PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Link against LLVM and Clang libraries
target_link_libraries(cilk2vitis PRIVATE
    LLVMCore
    clangTooling
    clangFrontend
    clangSerialization
    clangDriver
)
