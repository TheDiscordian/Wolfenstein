/*
** blake_gameswitches.h
**
**---------------------------------------------------------------------------
** Copyright 2026 TheDiscordian
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
*/

#ifndef __BLAKE_GAMESWITCHES_H__
#define __BLAKE_GAMESWITCHES_H__

// PS GAME SWITCHES (DOS gamestate flags toggled by CP_Switches, 3d_menu.c:1058):
// LIGHTING (view shading), REBA ATTACK INFO (the LINC attacker readout), SHOW
// CEILINGS and SHOW FLOORS.  All default on.
enum BlakeGameSwitch
{
	GS_LIGHTING,
	GS_ATTACK_INFOAREA,
	GS_DRAW_CEILING,
	GS_DRAW_FLOOR,
	GS_NUM
};

bool Blake_GetSwitch(int sw);
void Blake_SetSwitch(int sw, bool on);
bool Blake_ToggleSwitch(int sw);	// flips and returns the new state
bool& Blake_SwitchRef(int sw);		// live storage, for BooleanMenuItem

#endif
