cmake_minimum_required(VERSION 3.16)
project(otFPSI)

option(WITH_SHA "Enable SHA support (Requires the x86 SHA-NI)" ON) 

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -flto -Wall -Wextra")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra")

find_package(libOTe REQUIRED)
find_package(blake3 REQUIRED)

file(GLOB_RECURSE SRC_FILES *.cpp)
list(FILTER SRC_FILES EXCLUDE REGEX "CMakeFiles")

add_executable(otfpsi ${SRC_FILES})
target_link_libraries(otfpsi oc::libOTe blake3)

if(WITH_SHA)
    target_compile_definitions(otfpsi PUBLIC ENABLE_SHA)
    # Enable SHA256 Extension for util/prf/sha256.cpp
    set_source_files_properties(
        util/prf/sha256.cpp
        PROPERTIES COMPILE_FLAGS "-msha -msse4.1"
    )
endif()

# Enable AES-NI (for util/aes.hpp). See https://github.com/mrdcvlsc/AES.
target_compile_definitions(otfpsi PUBLIC USE_INTEL_AESNI)
target_compile_options(otfpsi PRIVATE -maes)
