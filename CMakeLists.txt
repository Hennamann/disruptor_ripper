cmake_minimum_required(VERSION 3.10)
project(disruptor_ripper C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# The browser level viewer is embedded into the executable and written next to the extracted levels.
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/viewer_html.c
    COMMAND ${CMAKE_COMMAND} -DIN=${CMAKE_CURRENT_SOURCE_DIR}/viewer/viewer.html
            -DOUT=${CMAKE_CURRENT_BINARY_DIR}/viewer_html.c -DSYM=viewer_html
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS viewer/viewer.html cmake/embed.cmake
    COMMENT "Embedding viewer/viewer.html")

add_executable(disruptor_ripper
    ${CMAKE_CURRENT_BINARY_DIR}/viewer_html.c
    src/main.c
    src/platform.c
    src/wad.c
    src/png.c
    src/psx.c
    src/adpcm.c
    src/sprites.c
    src/sounds.c
    src/level.c
    src/misc.c
)

if(MSVC)
    target_compile_definitions(disruptor_ripper PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(disruptor_ripper PRIVATE /W3)
else()
    target_compile_options(disruptor_ripper PRIVATE -Wall -Wextra)
endif()

install(TARGETS disruptor_ripper DESTINATION bin)
