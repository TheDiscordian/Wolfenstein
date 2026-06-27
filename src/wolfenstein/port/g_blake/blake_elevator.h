/*
** blake_elevator.h
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

#ifndef __BLAKE_ELEVATOR_H__
#define __BLAKE_ELEVATOR_H__

class FArchive;

// Floor lock table (bstone gamestuff.level[].locked); keyed by LevelNumber.
void Blake_FloorLocksNewGame();
void Blake_FloorEntered();
void Blake_FloorLockSerialize(FArchive &arc);
void Blake_FloorLocksLoadLegacy();

// PS teleport security (bstone gamestuff.level[].locked, inverted sense:
// floors start locked until their Security Cube is destroyed).
bool Blake_PsFloorUnlocked(int lvl);
void Blake_PsUnlockFloor(int lvl);
void Blake_PsSerialize(FArchive &arc);
void Blake_PsClear();

// Set by Elevator_SelectFloor; PlayLoop runs the panel between frames.
extern bool Blake_ElevatorRequested;
void Blake_ElevatorCheck();
// Step the player out of the elevator car on AOG floor arrival. Self-gated:
// a no-op unless an AOG elevator ride armed it.
void Blake_AlignPlayerInElevator();

// Overall mission ratio for the high-score table (bstone ss_justcalc).
int Blake_MissionRatio();

#endif
