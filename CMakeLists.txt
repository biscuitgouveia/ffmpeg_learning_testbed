cmake_minimum_required(VERSION 3.14)
project(FFmpegTestbed C CXX)
set(CMAKE_CXX_STANDARD 14)

add_subdirectory(lib/FFmpeg)

add_executable(FFmpegTestbed MACOSX_BUNDLE WIN32 src/testbed.c)

target_link_libraries(FFmpegTestbed FFmpeg)