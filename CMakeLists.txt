cmake_minimum_required(VERSION 2.8.11 FATAL_ERROR)
project(emu8051)

add_executable(emu8051 core.c
                       disasm.c
                       emu.c
                       logicboard.c
                       mainview.c
                       memeditor.c
                       opcodes.c
                       options.c
                       popups.c)

add_compile_options(
    "-Wall" "-Wextra" "-Wno-missing-field-initializers"
    "$<$<CONFIG:DEBUG>:-O0;-ggdb3;-DDEBUG_BUILD>"
    "$<$<CONFIG:RELEASE>:-Os;-Werror;-DRELEASE_BUILD>")

target_link_libraries(emu8051 ncurses)
