cmake_minimum_required(VERSION 3.20)
project(GameLangCompiler LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

add_executable(gamelang_compiler main.cpp)

llvm_map_components_to_libnames(llvm_libs
    Core
    Analysis
    ExecutionEngine
    InstCombine
    Object
    OrcJIT
    ScalarOpts
    Support
    TransformUtils
    native
)

target_link_libraries(gamelang_compiler PRIVATE ${llvm_libs})
