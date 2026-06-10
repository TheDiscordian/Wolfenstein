/*
** i_main.cpp
**
**---------------------------------------------------------------------------
** Copyright 2021 Braden Obrzut
** All rights reserved.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
**
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
**---------------------------------------------------------------------------
**
**
*/

#include <cstdlib>

#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
extern void OF_EarlyStartupScreen(int progress);
#include "of_ecwolf_bootlog.h"
#ifdef OF_BOOT_MARKERS
#include <cstdio>
#endif
#endif

#ifndef NO_GTK
#include <gtk/gtk.h>
bool GtkAvailable;
#endif

int main(int argc, char *argv[])
{
#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
#ifdef OF_BOOT_MARKERS
	printf("BOOT: main() entered — all global constructors completed\n");
#endif
	OF_BootLog("BOOT: main enter (all global ctors done)\n");
	OF_EarlyStartupScreen(1);
#endif

	// Set LC_NUMERIC environment variable in case some library decides to
	// clear the setlocale call at least this will be correct.
	// Note that the LANG environment variable is overridden by LC_*
	setenv("LC_NUMERIC", "C", 1);

#ifndef NO_GTK
	GtkAvailable = gtk_init_check(&argc, &argv);
#endif

	extern int WL_Main(int argc, char *argv[]);
	return WL_Main(argc, argv);
}
