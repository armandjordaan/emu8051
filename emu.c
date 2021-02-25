/* 8051 emulator
 * Copyright 2006 Jari Komppa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * (i.e. the MIT License)
 *
 * emu.c
 * Curses-based emulator front-end
 */

#ifdef _MSC_VER
#include <windows.h>
#undef MOUSE_MOVED
#else
#include <sys/time.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <getopt.h>

#ifdef __linux__
#include <curses.h>
#else
#include "curses.h"
#endif
#include "emu8051.h"
#include "emulator.h"

unsigned char history[HISTORY_LINES * (128 + 64 + sizeof(int))];


// current line in the history cyclic buffers
int historyline = 0;
// last known columns and rows; for screen resize detection
int oldcols, oldrows;
// are we in single-step or run mode
int runmode = 0;
// current run speed, lower is faster
int speed = 6;

// instruction count; needed to replay history correctly
unsigned int icount = 0;

// current clock count
unsigned int clocks = 0;

// currently active view
int view = MAIN_VIEW;

// old port out values
int p0out = 0;
int p1out = 0;
int p2out = 0;
int p3out = 0;

int breakpoint = -1;

// returns time in 1ms units
int getTick()
{
#ifdef _MSC_VER
    return GetTickCount();
#else
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000 + now.tv_usec / 1000;
#endif
}

void emu_sleep(int value)
{
#ifdef _MSC_VER
    Sleep(value);
#else
    usleep(value * 1000);
#endif
}

void setSpeed(int speed, int runmode)
{
    switch (speed)
    {
    case 7:
        slk_set(5, "+/-|.5Hz", 0);
        break;
    case 6:
        slk_set(5, "+/-|1Hz", 0);
        break;
    case 5:
        slk_set(5, "+/-|2Hz", 0);
        break;
    case 4:
        slk_set(5, "+/-|10Hz", 0);
        break;
    case 3:
        slk_set(5, "+/-|fast", 0);
        break;
    case 2:
        slk_set(5, "+/-|f+", 0);
        break;
    case 1:
        slk_set(5, "+/-|f++", 0);
        break;
    case 0:
        slk_set(5, "+/-|f*", 0);
        break;
    }

    if (runmode == 0)
    {
        slk_set(4, "r)un", 0);
        slk_refresh();
        nocbreak();
        cbreak();
        nodelay(stdscr, FALSE);
        return;
    }
    else
    {
        slk_set(4, "r)unning", 0);
        slk_refresh();
    }

    if (speed < 4)
    {
        nocbreak();
        cbreak();
        nodelay(stdscr, TRUE);
    }
    else
    {
        switch(speed)
        {
        case 7:
            halfdelay(20);
            break;
        case 6:
            halfdelay(10);
            break;
        case 5:
            halfdelay(5);
            break;
        case 4:
            halfdelay(1);
            break;
        }
    }
}




int emu_sfrread(struct em8051 *aCPU, int aRegister)
{
    int outputbyte = -1;

    if (view == LOGICBOARD_VIEW)
    {
        if (aRegister == REG_P0 + 0x80)
        {
            outputbyte = p0out;
        }
        if (aRegister == REG_P1 + 0x80)
        {
            outputbyte =  p1out;
        }
        if (aRegister == REG_P2 + 0x80)
        {
            outputbyte =  p2out;
        }
        if (aRegister == REG_P3 + 0x80)
        {
            outputbyte =  p3out;
        }
    }
    else
    {
        if (aRegister == REG_P0 + 0x80)
        {
            outputbyte = p0out = emu_readvalue(aCPU, "P0 port read", p0out, 2);
        }
        if (aRegister == REG_P1 + 0x80)
        {
            outputbyte = p1out = emu_readvalue(aCPU, "P1 port read", p1out, 2);
        }
        if (aRegister == REG_P2 + 0x80)
        {
            outputbyte = p2out = emu_readvalue(aCPU, "P2 port read", p2out, 2);
        }
        if (aRegister == REG_P3 + 0x80)
        {
            outputbyte = p3out = emu_readvalue(aCPU, "P3 port read", p3out, 2);
        }
    }
    if (outputbyte != -1)
    {
        if (opt_input_outputlow == 1)
        {
            // option: output 1 even though ouput latch is 0
            return outputbyte;
        }
        if (opt_input_outputlow == 0)
        {
            // option: output 0 if output latch is 0
            return outputbyte & aCPU->mSFR[aRegister - 0x80];
        }
        // option: dump random values for output bits with
        // output latches set to 0
        return outputbyte & aCPU->mSFR[aRegister - 0x80] |
            (rand() & ~aCPU->mSFR[aRegister - 0x80]);
    }
    return aCPU->mSFR[aRegister - 0x80];

}

void refreshview(struct em8051 *aCPU)
{
    change_view(aCPU, view);
}

void change_view(struct em8051 *aCPU, int changeto)
{
    switch (view)
    {
    case MAIN_VIEW:
        wipe_main_view();
        break;
    case LOGICBOARD_VIEW:
        wipe_logicboard_view();
        break;
    case MEMEDITOR_VIEW:
        wipe_memeditor_view();
        break;
    case OPTIONS_VIEW:
        wipe_options_view();
        break;
    }
    view = changeto;
    switch (view)
    {
    case MAIN_VIEW:
        build_main_view(aCPU);
        break;
    case LOGICBOARD_VIEW:
        build_logicboard_view(aCPU);
        break;
    case MEMEDITOR_VIEW:
        build_memeditor_view(aCPU);
        break;
    case OPTIONS_VIEW:
        build_options_view(aCPU);
        break;
    }
}

void print_help(const char * name) {
    fprintf(stderr, "Help:\n\n"
        "%s [options] [filename]\n\n"
        "Both the filename and options are optional. Available options:\n\n"
        "Option            Alternate   description\n"
        "-raw              -r          Load a raw flash dump\n"
        "-step_instruction -si         Step one instruction at a time\n"
        "-noexc_iret_sp    -nosp       Disable sp iret exception\n"
        "-noexc_iret_acc   -noacc      Disable acc iret exception\n"
        "-noexc_iret_psw   -nopsw      Disable pdw iret exception\n"
        "-noexc_acc_to_a   -noaa       Disable acc-to-a invalid instruction exception\n"
        "-noexc_stack      -nostk      Disable stack abnormal behaviour exception\n"
        "-noexc_invalid_op -noiop      Disable invalid opcode exception\n"
        "-iolowlow         If out pin is low, hi input from same pin is low\n"
        "-iolowrand        If out pin is low, hi input from same pin is random\n"
        "-clock=value      Set clock speed, in Hz\n",
	name
    );
}

int main(int parc, char ** pars)
{
    int ch = 0;
    struct em8051 emu;
    int i;
    int ticked = 1;

    memset(&emu, 0, sizeof(emu));
    emu.mCodeMem     = malloc(65536);
    emu.mCodeMemSize = 65536;
    emu.mExtData     = malloc(65536);
    emu.mExtDataSize = 65536;
    emu.mLowerData   = malloc(128);
    emu.mUpperData   = malloc(128);
    emu.mSFR         = malloc(128);
    emu.except       = &emu_exception;
    emu.sfrread      = &emu_sfrread;
    emu.xread = NULL;
    emu.xwrite = NULL;
    reset(&emu, 1);

    const struct option long_options[] = {
        {"raw", no_argument, &opt_raw, 1},
        {"step_instruction", no_argument, &opt_step_instruction, 1},
        {"si",               no_argument, &opt_step_instruction, 1},
        {"noexc_iret_sp", no_argument, &opt_exception_iret_sp, 0},
        {"nosp",          no_argument, &opt_exception_iret_sp, 0},
        {"noexc_iret_acc", no_argument, &opt_exception_iret_acc, 0},
        {"noacc",          no_argument, &opt_exception_iret_acc, 0},
        {"noexc_iret_psw", no_argument, &opt_exception_iret_psw, 0},
        {"nopsw",          no_argument, &opt_exception_iret_psw, 0},
        {"noexc_acc_to_a", no_argument, &opt_exception_acc_to_a, 0},
        {"noaa",           no_argument, &opt_exception_acc_to_a, 0},
        {"noexc_stack", no_argument, &opt_exception_stack, 0},
        {"nostk",       no_argument, &opt_exception_stack, 0},
        {"noexc_invalid_op", no_argument, &opt_exception_invalid, 0},
        {"noiop",            no_argument, &opt_exception_invalid, 0},
        {"iolowlow", no_argument, &opt_input_outputlow, 0},
        {"iolowrand", no_argument, &opt_input_outputlow, 2},
        {"clock", required_argument, NULL, 'c'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0},
    };

    if (parc > 1)
    {
        int c;
        while ((c = getopt_long_only(parc, pars, "", long_options, NULL)) != -1)
        {
            switch (c) {
            case 0:
                /* Long option already handled */
                break;
            case 'c':
                opt_clock_select = 12;
                opt_clock_hz = strtol(optarg, NULL, 10);
                if (opt_clock_hz <= 0) {
                    fprintf(stderr, "Error: Invalid clock speed: %s\n\n", optarg);
                    print_help(pars[0]);
                    return -1;
                }
                break;
            case 'h':
                print_help(pars[0]);
                return -1;
            case '?':
                fprintf(stderr, "\n");
                print_help(pars[0]);
                return -1;
            default:
                fprintf(stderr, "Error: getopt returned character code 0%o ??\n", c);
                return -1;
            }
        }

        if (optind < parc)
        {
            strcpy(filename, pars[optind]);
            int load_result = opt_raw ? load_raw(&emu, filename) :
                                        load_obj(&emu, filename);
            if (load_result != 0)
            {
                printf("File '%s' load failure, err %d\n\n",
                       filename, load_result);
                return -1;
            }
        }
    }

    //  Initialize ncurses

    slk_init(1);
    if ( (initscr()) == NULL ) {
	    fprintf(stderr, "Error initialising ncurses.\n");
	    exit(EXIT_FAILURE);
    }

    slk_set(1, "h)elp", 0);
    slk_set(2, "l)oad", 0);
    slk_set(3, "spc=step", 0);
    slk_set(4, "r)un", 0);
    slk_set(6, "v)iew", 0);
    slk_set(7, "home=rst", 0);
    slk_set(8, "s-Q)quit", 0);
    setSpeed(speed, runmode);


    //  Switch of echoing and enable keypad (for arrow keys etc)

    cbreak(); // no buffering
    noecho(); // no echoing
    keypad(stdscr, TRUE); // cursors entered as single characters

    build_main_view(&emu);

    // Loop until user hits 'shift-Q'

    do
    {
        if (LINES != oldrows ||
            COLS != oldcols)
        {
            refreshview(&emu);
        }
        switch (ch)
        {
        case KEY_F(1):
            change_view(&emu, 0);
            break;
        case KEY_F(2):
            change_view(&emu, 1);
            break;
        case KEY_F(3):
            change_view(&emu, 2);
            break;
        case KEY_F(4):
            change_view(&emu, 3);
            break;
        case 'v':
            change_view(&emu, (view + 1) % 4);
            break;
        case 'k':
            if (breakpoint != -1)
            {
                breakpoint = -1;
                emu_popup(&emu, "Breakpoint", "Breakpoint cleared.");
            }
            else
            {
                breakpoint = emu_readvalue(&emu, "Set Breakpoint", emu.mPC, 4);
            }
            break;
        case 'g':
            emu.mPC = emu_readvalue(&emu, "Set Program Counter", emu.mPC, 4);
            break;
        case 'h':
            emu_help(&emu);
            break;
        case 'l':
            emu_load(&emu);
            break;
        case ' ':
            runmode = 0;
            setSpeed(speed, runmode);
            break;
        case 'r':
            if (runmode)
            {
                runmode = 0;
                setSpeed(speed, runmode);
            }
            else
            {
                runmode = 1;
                setSpeed(speed, runmode);
            }
            break;
#ifdef __PDCURSES__
        case PADPLUS:
#endif
        case '+':
            speed--;
            if (speed < 0)
                speed = 0;
            setSpeed(speed, runmode);
            break;
#ifdef __PDCURSES__
        case PADMINUS:
#endif
        case '-':
            speed++;
            if (speed > 7)
                speed = 7;
            setSpeed(speed, runmode);
            break;
        case KEY_HOME:
            if (emu_reset(&emu))
            {
                clocks = 0;
                ticked = 1;
            }
            break;
        case KEY_END:
            clocks = 0;
            ticked = 1;
            break;
        default:
            // by default, send keys to the current view
            switch (view)
            {
            case MAIN_VIEW:
                mainview_editor_keys(&emu, ch);
                break;
            case LOGICBOARD_VIEW:
                logicboard_editor_keys(&emu, ch);
                break;
            case MEMEDITOR_VIEW:
                memeditor_editor_keys(&emu, ch);
                break;
            case OPTIONS_VIEW:
                options_editor_keys(&emu, ch);
                break;
            }
            break;
        }

        if (ch == 32 || runmode)
        {
            int targettime;
            unsigned int targetclocks;
            targetclocks = 1;
            targettime = getTick();

            if (speed == 2 && runmode)
            {
                targettime += 1;
                targetclocks += (opt_clock_hz / 12000) - 1;
            }
            if (speed < 2 && runmode)
            {
                targettime += 10;
                targetclocks += (opt_clock_hz / 1200) - 1;
            }

            do
            {
                int old_pc;
                old_pc = emu.mPC;
                if (opt_step_instruction)
                {
                    ticked = 0;
                    while (!ticked)
                    {
                        targetclocks--;
                        clocks += 12;
                        ticked = tick(&emu);
                        logicboard_tick(&emu);
                    }
                }
                else
                {
                    targetclocks--;
                    clocks += 12;
                    ticked = tick(&emu);
                    logicboard_tick(&emu);
                }

                if (emu.mPC == breakpoint)
                    emu_exception(&emu, -1);

                if (ticked)
                {
                    icount++;

                    historyline = (historyline + 1) % HISTORY_LINES;

                    memcpy(history + (historyline * (128 + 64 + sizeof(int))), emu.mSFR, 128);
                    memcpy(history + (historyline * (128 + 64 + sizeof(int))) + 128, emu.mLowerData, 64);
                    memcpy(history + (historyline * (128 + 64 + sizeof(int))) + 128 + 64, &old_pc, sizeof(int));
                }
            }
            while (targettime > getTick() && targetclocks > 0);

            while (targettime > getTick())
            {
                emu_sleep(1);
            }
        }

        switch (view)
        {
        case MAIN_VIEW:
            mainview_update(&emu);
            break;
        case LOGICBOARD_VIEW:
            logicboard_update(&emu);
            break;
        case MEMEDITOR_VIEW:
            memeditor_update(&emu);
            break;
        case OPTIONS_VIEW:
            options_update(&emu);
            break;
        }
    }
    while ( (ch = getch()) != 'Q' );

    endwin();

    return EXIT_SUCCESS;
}
