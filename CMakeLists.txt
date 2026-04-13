cmake_minimum_required(VERSION 3.23)
project(text_editor C)

set(CMAKE_C_STANDARD 99)

include_directories(include)

add_executable(ted
    src/main.c
    src/editor.c
    src/gap_buffer.c
)

target_link_libraries(ted ncurses)

