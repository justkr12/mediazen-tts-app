cmake_minimum_required(VERSION 3.10.2)
project(tts_jni LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(tts_jni SHARED tts_jni.cpp)

# Link only against system libraries.
# Other libraries are loaded dynamically via dlopen in tts_jni.cpp
target_link_libraries(tts_jni
    dl
    log
    android
    OpenSLES
)

target_include_directories(tts_jni PRIVATE ${CMAKE_SOURCE_DIR})