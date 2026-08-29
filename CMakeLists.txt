cmake_minimum_required(VERSION 3.20 FATAL_ERROR)
project(XXC VERSION 0.1 LANGUAGES CXX C)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 23)
# set(CMAKE_CXX_SCAN_FOR_MODULES ON)

find_package(LLVM CONFIG QUIET)

file(GLOB_RECURSE cxx_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx")

add_executable(${PROJECT_NAME} ${cxx_sources})
# collect_cxx_modules(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
    add_definitions(${LLVM_DEFINITIONS})
    target_include_directories(${PROJECT_NAME} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include" ${LLVM_INCLUDE_DIRS})
    llvm_map_components_to_libnames(llvm_libs support core irreader nativecodegen)
else()
    message(WARNING "LLVM not found. Building without LLVM support (lexer/preprocessor only)")
endif()

target_compile_options(${PROJECT_NAME} PRIVATE 
    -Wall
    -Wextra
    -Werror
    -Wpedantic
    -Wconversion
    -Wshadow
    -Wcast-qual
    -Wwrite-strings
    -Wnull-dereference
)
