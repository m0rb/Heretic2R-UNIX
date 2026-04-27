//
// main.c -- Welcome To Your Doom, Heretic!
//
// Copyright (C) 1997-2001 Id Software, Inc.
// Copyright (C) 2011 Yamagi Burmeister
// Copyright (C) 1998 Raven Software
//
// Heretic2R UNIX port by morb
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <execinfo.h>

#include "Quake2Main.h"
#include "qcommon.h"

static void signal_handler(int sig)
{
    fprintf(stderr, "Received signal %d, shutting down...\n", sig);
    Sys_Quit();
}

static void sigsegv_handler(int sig, siginfo_t* info, void* ctx)
{
    void* bt[32];
    int n = backtrace(bt, 32);
    fprintf(stderr, "\n=== SIGSEGV at %p ===\n", info->si_addr);
    backtrace_symbols_fd(bt, n, STDERR_FILENO);
    fflush(stderr);
    signal(SIGSEGV, SIG_DFL);
    raise(SIGSEGV);
}

static void install_signal_handlers(void)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGQUIT, signal_handler);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = sigsegv_handler;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGSEGV, &sa, NULL);
}

int main(int argc, char** argv)
{
    printf("Heretic2R-UNIX\n");

    install_signal_handlers();

    Qcommon_Init(argc, argv);

    long long oldtime = Sys_Microseconds();
    while (1)
    {
        long long newtime = Sys_Microseconds();
        int usec = (int)(newtime - oldtime);

        if (usec > 0)
        {
            curtime = (int)(newtime / 1000LL); 
            Qcommon_Frame(usec);
        }

        oldtime = newtime;
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL); 
    }

    return 0;
}
