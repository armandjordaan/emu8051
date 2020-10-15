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
target_link_libraries(emu8051 ncurses)
