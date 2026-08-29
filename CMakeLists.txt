cmake_minimum_required(VERSION 4.4 FATAL_ERROR)
project(XXC VERSION 0.1 LANGUAGES CXX ASM_NASM)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_CXX_STANDARD 26)
# set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if (NOT WIN32)
    set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -f elf64")
endif()

set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -g")

file(GLOB_RECURSE cxx_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cxx")
file(GLOB_RECURSE asm_sources "${CMAKE_CURRENT_SOURCE_DIR}/src/*.asm")

add_executable(${PROJECT_NAME} ${cxx_sources} ${asm_sources})
# collect_cxx_modules(${PROJECT_NAME} ${CMAKE_SOURCE_DIR}/include)
target_include_directories(${PROJECT_NAME} PUBLIC  "${CMAKE_CURRENT_SOURCE_DIR}/include")

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
