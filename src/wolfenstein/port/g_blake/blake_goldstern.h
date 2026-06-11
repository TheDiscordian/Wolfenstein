#ifndef __BLAKE_GOLDSTERN_H__
#define __BLAKE_GOLDSTERN_H__

// Called at map load before objects are parsed.
void Goldstern_Clear();
// Records a Goldfire spawn site (map plane 1 code 124 or 141). Immediate
// sites (141, PS only) also warp him in on the first tic.
void Goldstern_AddSite(unsigned int x, unsigned int y, bool immediate);
// Per-tic spawn timer; runs the warp-in hunt while he is off the map.
void Goldstern_Tick();

#endif
