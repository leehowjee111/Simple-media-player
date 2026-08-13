cmake_minimum_required(VERSION 3.10)
project(SDL2_test VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address -fsanitize=leak")
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig)
pkg_check_modules(FFMPEG REQUIRED
                  libavformat
                  libavutil
                  libavcodec
                  libswresample
                  )
pkg_check_modules(SDL2 REQUIRED sdl2)
add_executable(SDL2_test main.cpp
                        src/FrameQueue.cpp
                        src/AudioBuffer.cpp
                        src/demuxer.cpp
                        src/player.cpp
                        src/video_codec.cpp
                        src/audio_codec.cpp
                        src/AudioPacket.cpp
                        src/FramePacket.cpp)

target_link_libraries(SDL2_test PRIVATE ${FFMPEG_LIBRARIES} ${SDL2_LIBRARIES})
target_include_directories(SDL2_test PRIVATE ${FFMPEG_INCLUDE_DIRS}
                                              ${SDL2_INCLUDE_DIRS})
target_compile_options(SDL2_test PRIVATE -Wall -Wextra)
