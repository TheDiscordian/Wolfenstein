#include "textures/textures.h"
#include "c_cvars.h"
#include "id_ca.h"
#include "gamemap.h"
#include "g_mapinfo.h"
#include "wl_def.h"
#include "wl_draw.h"
#include "wl_main.h"
#include "wl_shade.h"
#include "r_data/colormaps.h"
#include "of_ecwolf_gpu.h"

#include <climits>
#include <string.h>

extern int viewshift;
extern fixed viewz;

// Pure: map a depth value tz and a frame-constant shade to the final palette
// byte.  tz must equal planeVis*rowDistance (int32); shade =
// LIGHT2SHADE(gLevelLight + r_extralight).  Shared so the value is provably
// identical on both targets; only the device backdrop path calls it.
static inline byte R_SolidPlaneColorForTz(int tz, int shade, byte baseColor)
{
	const int shadeIndex = GETPALOOKUP(tz, shade);
	return NormalLight.Maps[(shadeIndex << 8) + baseColor];
}

struct SolidTextureCacheEntry
{
	FTexture *texture;
	bool checked;
	bool solid;
	byte color;
};

struct SolidPlaneCacheEntry
{
	const GameMap *map;
	bool checked;
	bool solid;
	byte color;
};

static SolidTextureCacheEntry solidTextureCache[16];
static unsigned int solidTextureCacheNext;
static SolidPlaneCacheEntry solidPlaneCache[2];

static bool R_TextureSolidColor(FTextureID textureID, byte &color)
{
	if(!textureID.isValid())
		return false;

	FTexture *texture = TexMan(textureID);
	if(texture == NULL || texture->bMasked || texture->CheckModified())
		return false;

	for(unsigned int i = 0;i < countof(solidTextureCache);++i)
	{
		if(solidTextureCache[i].texture == texture && solidTextureCache[i].checked)
		{
			if(solidTextureCache[i].solid)
				color = solidTextureCache[i].color;
			return solidTextureCache[i].solid;
		}
	}

	const byte *pixels = texture->GetPixels();
	const int pixelCount = texture->GetWidth() * texture->GetHeight();
	if(pixels == NULL || pixelCount <= 0)
		return false;

	const byte first = pixels[0];
	bool solid = true;
	for(int i = 1;i < pixelCount;++i)
	{
		if(pixels[i] != first)
		{
			solid = false;
			break;
		}
	}

	SolidTextureCacheEntry &entry =
		solidTextureCache[solidTextureCacheNext++ % countof(solidTextureCache)];
	entry.texture = texture;
	entry.checked = true;
	entry.solid = solid;
	entry.color = first;

	if(solid)
		color = first;
	return solid;
}

static bool R_GetUniformPlaneSolidColor(bool floor, byte &color)
{
	if(map == NULL || map->NumPlanes() == 0)
		return false;

	SolidPlaneCacheEntry &cache = solidPlaneCache[floor ? 0 : 1];
	if(cache.checked && cache.map == map)
	{
		if(cache.solid)
			color = cache.color;
		return cache.solid;
	}

	cache.map = map;
	cache.checked = true;
	cache.solid = false;
	cache.color = 0;

	const GameMap::Header &header = map->GetHeader();
	if(header.width == 0 || header.height == 0)
		return false;

	byte planeColor = 0;
	bool haveColor = false;

	for(unsigned int y = 0;y < header.height;++y)
	{
		for(unsigned int x = 0;x < header.width;++x)
		{
			const MapSpot spot = map->GetSpot(x, y, 0);
			if(spot == NULL)
				return false;
			if(spot->sector == NULL)
				continue;

			const FTextureID current =
				spot->sector->texture[floor ? MapSector::Floor : MapSector::Ceiling];
			if(!current.isValid())
				return false;

			byte currentColor = 0;
			if(!R_TextureSolidColor(current, currentColor))
				return false;

			if(!haveColor)
			{
				planeColor = currentColor;
				haveColor = true;
			}
			else if(currentColor != planeColor)
			{
				return false;
			}
		}
	}

	if(!haveColor && levelInfo != NULL)
	{
		const FTextureID defaultTexture =
			levelInfo->DefaultTexture[floor ? MapSector::Floor : MapSector::Ceiling];
		haveColor = R_TextureSolidColor(defaultTexture, planeColor);
	}

	if(haveColor)
	{
		cache.solid = true;
		cache.color = planeColor;
		color = planeColor;
		return true;
	}
	return false;
}

static bool R_GetDefaultPlaneSolidColor(bool floor, byte &color)
{
	if(levelInfo == NULL)
		return false;

	const FTextureID texture =
		levelInfo->DefaultTexture[floor ? MapSector::Floor : MapSector::Ceiling];
	return R_TextureSolidColor(texture, color);
}

#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
static FTexture *R_GetDefaultPlaneTexture(bool floor)
{
	if(levelInfo == NULL)
		return NULL;

	const FTextureID texture =
		levelInfo->DefaultTexture[floor ? MapSector::Floor : MapSector::Ceiling];
	if(!texture.isValid())
		return NULL;
	return TexMan(texture);
}

static bool R_TextureFirstColor(FTexture *texture, byte &color)
{
	if(texture == NULL)
		return false;

	const byte *pixels = texture->GetPixels();
	if(pixels == NULL || texture->GetWidth() <= 0 || texture->GetHeight() <= 0)
		return false;

	color = pixels[0];
	return true;
}

static bool R_ClearSolidBackdropHalfGPU(byte *vbuf, unsigned vbufPitch,
	int yStart, int yEnd, int horizon, fixed planeheight, byte baseColor)
{
	if(planeheight == 0 || yStart >= yEnd)
		return true;

	/* planeVis stays hoisted -- FixedDiv is a soft 64-bit divide here.  The
	 * shade is frame-constant and tz = planeVis*rowDistance changes by +-planeVis
	 * per row, so both come out of the per-row loop (was a LIGHT2SHADE plus a
	 * 64-bit FixedMul every row, twice per run).  Each call covers one pure half
	 * -- ceiling [0,horizon) with rowDistance horizon-y (decreasing), floor
	 * [horizon,viewheight) with rowDistance y-horizon+1 (increasing) -- so the
	 * step sign is uniform.  FixedMul(planeVis, rd<<FRACBITS) == int32
	 * planeVis*rd because rd<<16 has no low bits to round; the per-row clamp
	 * (rowDistance<=0) is dead at both call sites (rd0 is always >= 1). */
	const fixed planeVis = FixedDiv(r_depthvisibility, abs(planeheight));
	const int shade = LIGHT2SHADE(gLevelLight + r_extralight);
	const bool ceiling = yStart < horizon;
	const int rd0 = ceiling ? horizon - yStart : yStart - horizon + 1;
	const int step = ceiling ? -(int)planeVis : (int)planeVis;
	int tz = (int)((int32_t)planeVis * (int32_t)rd0);

#if OF_BLAKE_FL_FLAT_EXPERIMENT
	// MEASUREMENT EXPERIMENT (not a shipping change): clear the whole half in ONE
	// flat ClearRect (no depth-fog bands) -> 2 clear commands/frame instead of
	// ~60.  If fl collapses, the floor cost is confirmed as GPU command-pipeline
	// backpressure from the per-band command count.  Floor looks flat-shaded
	// while enabled.  Toggle with -DOF_BLAKE_FL_FLAT_EXPERIMENT=1.
	{
		const byte flatColor = R_SolidPlaneColorForTz(tz, shade, baseColor);
		return OF_WolfGPU_ClearRect(vbuf + yStart * (int)vbufPitch, viewwidth,
			yEnd - yStart, flatColor);
	}
#endif

	for(int y = yStart; y < yEnd;)
	{
		const byte rowColor = R_SolidPlaneColorForTz(tz, shade, baseColor);

		int runEnd = y + 1;
		int tzr = tz;
		while(runEnd < yEnd)
		{
			tzr += step;
			if(R_SolidPlaneColorForTz(tzr, shade, baseColor) != rowColor)
				break;
			++runEnd;
		}

		if(!OF_WolfGPU_ClearRect(vbuf + y * (int)vbufPitch, viewwidth,
			runEnd - y, rowColor))
		{
			return false;
		}

		tz += step * (runEnd - y);
		y = runEnd;
	}
	return true;
}

static bool R_DrawTexturedBackdropHalfGPU(byte *vbuf, unsigned vbufPitch,
	int yStart, int yEnd, int horizon, fixed planeheight, bool floor,
	FTexture *texture)
{
	if(planeheight == 0 || yStart >= yEnd)
		return true;
	if(texture == NULL || texture->bMasked)
		return false;

	const int texwidth = texture->GetWidth();
	const int texheight = texture->GetHeight();
	const byte *tex = texture->GetPixels();
	if(tex == NULL || texwidth != 64 || texheight != 64 ||
		texture->xScale != FRACUNIT || texture->yScale != FRACUNIT)
	{
		return false;
	}

	OF_WolfGPU_PreloadSource(tex, (uint32_t)(texwidth * texheight));

	const int viewxFrac = (viewx & (FRACUNIT - 1)) << 8;
	const int viewyFrac = (viewy & (FRACUNIT - 1)) << 8;
	const fixed planenumerator = abs(FixedMul(heightnumerator, planeheight));
	const int shade = LIGHT2SHADE(gLevelLight + r_extralight);
	const fixed planeVis = FixedDiv(r_depthvisibility, abs(planeheight));

	for(int y = yStart; y < yEnd; ++y)
	{
		const int rowDistance = floor ? y - horizon + 1 : horizon - y;
		if(rowDistance <= 0)
			continue;

		const fixed dist = (planenumerator / rowDistance) << 8;
		fixed gu = viewxFrac + FixedMul(dist, viewcos);
		fixed gv = -viewyFrac + FixedMul(dist, viewsin);
		const fixed texStep = dist / scale;
		const fixed du = FixedMul(texStep, viewsin);
		const fixed dv = -FixedMul(texStep, viewcos);
		gu -= (viewwidth >> 1) * du;
		gv -= (viewwidth >> 1) * dv;

		const int tz = FixedMul(planeVis, rowDistance << FRACBITS);
		const int shadeIndex = GETPALOOKUP(tz, shade);
		if(!OF_WolfGPU_DrawSpan(vbuf + y * (int)vbufPitch, viewwidth,
			tex, 64, 64, -gv >> 2, gu >> 2, -dv >> 2, du >> 2,
			shadeIndex))
		{
			return false;
		}
	}
	return true;
}

static bool R_DrawPlaneBackdropHalfGPU(byte *vbuf, unsigned vbufPitch,
	int yStart, int yEnd, int horizon, fixed planeheight, bool floor,
	byte solidColor, bool solid, FTexture *texture)
{
	if(solid)
	{
		return R_ClearSolidBackdropHalfGPU(vbuf, vbufPitch, yStart, yEnd,
			horizon, planeheight, solidColor);
	}
	if(R_DrawTexturedBackdropHalfGPU(vbuf, vbufPitch, yStart, yEnd,
		horizon, planeheight, floor, texture))
	{
		return true;
	}

	byte fallbackColor;
	if(R_TextureFirstColor(texture, fallbackColor))
	{
		return R_ClearSolidBackdropHalfGPU(vbuf, vbufPitch, yStart, yEnd,
			horizon, planeheight, fallbackColor);
	}
	return false;
}

// Measurement-only diagnostic counters (defined in of_ecwolf_gpu.cpp): record
// per frame why the GPU backdrop path is or is not taken, so the bootlog perf
// line can localize fl to GPU vs CPU fallback.
extern uint32_t of_fl_dbg_gpu_true;
extern uint32_t of_fl_dbg_active;
extern uint32_t of_fl_dbg_bail;
extern uint32_t of_fl_dbg_solid;

bool DrawFloorAndCeilingBackdropGPU(byte *vbuf, unsigned vbufPitch)
{
	const bool active = OF_WolfGPU_IsActive();
	if(active)
		of_fl_dbg_active++;
	if(!active || map == NULL || map->NumPlanes() == 0)
	{
		of_fl_dbg_bail = !active ? 1u : (map == NULL ? 2u : 3u);
		return false;
	}

	byte floorColor;
	byte ceilingColor;
	bool solidFloor = R_GetUniformPlaneSolidColor(true, floorColor) ||
		R_GetDefaultPlaneSolidColor(true, floorColor);
	bool solidCeiling = R_GetUniformPlaneSolidColor(false, ceilingColor) ||
		R_GetDefaultPlaneSolidColor(false, ceilingColor);
	of_fl_dbg_solid = (solidCeiling ? 2u : 0u) | (solidFloor ? 1u : 0u);

	FTexture *floorTexture = solidFloor ? NULL : R_GetDefaultPlaneTexture(true);
	FTexture *ceilingTexture = solidCeiling ? NULL :
		R_GetDefaultPlaneTexture(false);

	int horizon = (viewheight >> 1) - viewshift;
	if(horizon < 0)
		horizon = 0;
	if(horizon > viewheight)
		horizon = viewheight;

	if(!R_DrawPlaneBackdropHalfGPU(vbuf, vbufPitch, 0, horizon, horizon,
		viewz + (map->GetPlane(0).depth << FRACBITS), false,
		ceilingColor, solidCeiling, ceilingTexture))
	{
		of_fl_dbg_bail = 4u;
		return false;
	}
	if(!R_DrawPlaneBackdropHalfGPU(vbuf, vbufPitch, horizon, viewheight,
		horizon, viewz, true, floorColor, solidFloor, floorTexture))
	{
		of_fl_dbg_bail = 5u;
		return false;
	}
	of_fl_dbg_bail = 0u;
	of_fl_dbg_gpu_true++;
	return true;
}
#else
bool DrawFloorAndCeilingBackdropGPU(byte *, unsigned)
{
	return false;
}
#endif

static void R_DrawSolidPlane(byte *vbuf, unsigned vbufPitch, int min_wallheight,
	int halfheight, fixed planeheight, byte baseColor)
{
	if(planeheight == 0)
		return;

	const fixed heightFactor = abs(planeheight)>>8;
	int y0 = ((min_wallheight*heightFactor)>>FRACBITS) - abs(viewshift);
	if(y0 > halfheight)
		return;
	if(y0 <= 0)
		y0 = 1;

	fixed planenumerator = FixedMul(heightnumerator, planeheight);
	const bool floor = planenumerator < 0;
	byte *tex_offset;
	int tex_offsetPitch;
	if(floor)
	{
		tex_offset = vbuf + (signed)vbufPitch * (halfheight + y0);
		tex_offsetPitch = vbufPitch;
		planenumerator *= -1;
	}
	else
	{
		tex_offset = vbuf + (signed)vbufPitch * (halfheight - y0 - 1);
		tex_offsetPitch = -vbufPitch;
	}

	const int shade = LIGHT2SHADE(gLevelLight + r_extralight);
	const fixed planeVis = FixedDiv(r_depthvisibility, abs(planeheight));
	for(int y = y0;floor ? y+halfheight < viewheight : y < halfheight; ++y, tex_offset += tex_offsetPitch)
	{
		if(floor ? (y+halfheight < 0) : (y < halfheight - viewheight))
		{
			continue;
		}

		const int tz = FixedMul(planeVis, abs(((halfheight)<<16) - ((halfheight-y)<<16)));
		const int shadeIndex = GETPALOOKUP(tz, shade);
		const byte rowColor = NormalLight.Maps[(shadeIndex<<8) + baseColor];

		unsigned int x = 0;
		while(x < (unsigned)viewwidth)
		{
			while(x < (unsigned)viewwidth &&
				((wallheight[x]*heightFactor)>>FRACBITS) > y)
			{
				++x;
			}

			const unsigned int start = x;
			while(x < (unsigned)viewwidth &&
				((wallheight[x]*heightFactor)>>FRACBITS) <= y)
			{
				++x;
			}

			if(x > start)
			{
#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
				if(OF_WolfGPU_IsActive())
				{
					if(OF_WolfGPU_ClearSpan(tex_offset + start, x - start, rowColor))
						continue;
					continue;
				}
#endif
				memset(tex_offset + start, rowColor, x - start);
			}
		}
	}
}

static void R_DrawPlane(byte *vbuf, unsigned vbufPitch, int min_wallheight, int halfheight, fixed planeheight)
{
	fixed dist;                                // distance to row projection
	fixed tex_step;                            // global step per one screen pixel
	fixed gu, gv, du, dv;                      // global texture coordinates
	const byte *tex = NULL;
	int texwidth = 0, texheight = 0;
	fixed texxscale = 0, texyscale = 0;
	FTextureID lasttex;
	byte *tex_offset;
	bool useOptimized = false;
	bool isMasked = false;

	if(planeheight == 0) // Eye level
		return;

	const fixed heightFactor = abs(planeheight)>>8;
	int y0 = ((min_wallheight*heightFactor)>>FRACBITS) - abs(viewshift);
	if(y0 > halfheight)
		return; // view obscured by walls
	if(y0 <= 0) y0 = 1; // don't let division by zero

	lasttex.SetInvalid();

	const unsigned int mapwidth = map->GetHeader().width;
	const unsigned int mapheight = map->GetHeader().height;

	fixed planenumerator = FixedMul(heightnumerator, planeheight);
	const bool floor = planenumerator < 0;
	int tex_offsetPitch;
	if(floor)
	{
		tex_offset = vbuf + (signed)vbufPitch * (halfheight + y0);
		tex_offsetPitch = vbufPitch-viewwidth;
		planenumerator *= -1;
	}
	else
	{
		tex_offset = vbuf + (signed)vbufPitch * (halfheight - y0 - 1);
		tex_offsetPitch = -viewwidth-vbufPitch;
	}

	// Break viewx/viewy apart so we can use the fractional part for texel selection without overflowing.
	const int viewxTile = viewx>>FRACBITS;
	const int viewxFrac = (viewx&(FRACUNIT-1))<<8; // 8.24
	const int viewyTile = viewy>>FRACBITS;
	const int viewyFrac = (viewy&(FRACUNIT-1))<<8; // 8.24

	unsigned int oldmapx = INT_MAX, oldmapy = INT_MAX;
	const byte* curshades = NormalLight.Maps;
	const int shade = LIGHT2SHADE(gLevelLight + r_extralight);
	const fixed planeVis = FixedDiv(r_depthvisibility, abs(planeheight));
	// draw horizontal lines
	for(int y = y0;floor ? y+halfheight < viewheight : y < halfheight; ++y, tex_offset += tex_offsetPitch)
	{
		if(floor ? (y+halfheight < 0) : (y < halfheight - viewheight))
		{
			tex_offset += viewwidth;
			continue;
		}

		// Shift in some extra bits so that we don't get spectacular round off.
		dist = (planenumerator / (y + 1))<<8;
		gu =  viewxFrac + FixedMul(dist, viewcos);
		gv = -viewyFrac + FixedMul(dist, viewsin);
		tex_step = dist / scale;
		du =  FixedMul(tex_step, viewsin);
		dv = -FixedMul(tex_step, viewcos);
		gu -= (viewwidth >> 1) * du;
		gv -= (viewwidth >> 1) * dv; // starting point (leftmost)

		// Depth fog
		const int tz = FixedMul(planeVis, abs(((halfheight)<<16) - ((halfheight-y)<<16)));
		const int shadeIndex = GETPALOOKUP(tz, shade);
		curshades = &NormalLight.Maps[shadeIndex<<8];

#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
		bool gpuActive = OF_WolfGPU_IsActive();
		bool gpuSpanActive = false;
		byte *gpuSpanDest = NULL;
		const byte *gpuSpanTex = NULL;
		int gpuSpanCount = 0;
		int gpuSpanS = 0;
		int gpuSpanT = 0;
		const int gpuSpanSStep = -dv >> 2;
		const int gpuSpanTStep = du >> 2;

		auto flushGpuSpan = [&]() {
			if(!gpuSpanActive)
				return;
			if(!OF_WolfGPU_DrawSpan(gpuSpanDest, gpuSpanCount, gpuSpanTex,
				64, 64, gpuSpanS, gpuSpanT, gpuSpanSStep, gpuSpanTStep,
				shadeIndex))
			{
				gpuActive = false;
			}
			gpuSpanActive = false;
			gpuSpanCount = 0;
			gpuSpanTex = NULL;
		};
#endif

		for(unsigned int x = 0;x < (unsigned)viewwidth; ++x, ++tex_offset)
		{
			if(((wallheight[x]*heightFactor)>>FRACBITS) <= y)
			{
				unsigned int curx = viewxTile + (gu >> (TILESHIFT+8));
				unsigned int cury = viewyTile + (-(gv >> (TILESHIFT+8)) - 1);

				if(curx != oldmapx || cury != oldmapy)
				{
					oldmapx = curx;
					oldmapy = cury;
					const MapSpot spot = map->GetSpot(oldmapx%mapwidth, oldmapy%mapheight, 0);

					FTextureID curtex = spot->sector ? spot->sector->texture[floor ? MapSector::Floor : MapSector::Ceiling] : FNullTextureID();

					if (curtex != lasttex)
					{
						lasttex = curtex;
						if(curtex.isValid())
						{
							FTexture * const texture = TexMan(curtex);
							tex = texture->GetPixels();
							texwidth = texture->GetWidth();
							texheight = texture->GetHeight();
							texxscale = texture->xScale>>10;
							texyscale = -texture->yScale>>10;

							useOptimized = texwidth == 64 && texheight == 64 && texxscale == FRACUNIT>>10 && texyscale == -FRACUNIT>>10;
							isMasked = texture->bMasked;
						}
						else
							tex = NULL;
					}
				}

				if(tex)
				{
#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
					if(gpuActive && useOptimized && !isMasked)
					{
						if(!gpuSpanActive || gpuSpanTex != tex)
						{
							flushGpuSpan();
							gpuSpanActive = true;
							gpuSpanDest = tex_offset;
							gpuSpanTex = tex;
							gpuSpanCount = 0;
							gpuSpanS = -gv >> 2;
							gpuSpanT = gu >> 2;
						}
						gpuSpanCount++;
						gu += du;
						gv += dv;
						continue;
					}
					flushGpuSpan();
					gu += du;
					gv += dv;
					continue;
#endif
					unsigned texoffs;
					if(useOptimized)
					{
						const int u = (gu>>18) & 63;
						const int v = (-gv>>18) & 63;
						texoffs = (u * 64) + v;
					}
					else
					{
						const int u = (FixedMul((viewxTile<<16)+(gu>>8)-512, texxscale)) & (texwidth-1);
						const int v = (FixedMul((viewyTile<<16)-(gv>>8)+512, texyscale)) & (texheight-1);
						texoffs = (u * texheight) + v;
					}

					if(isMasked)
					{
						if(const byte c = tex[texoffs])
							*tex_offset = curshades[c];
					}
					else
					{
						*tex_offset = curshades[tex[texoffs]];
					}
				}
#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
				else
				{
					flushGpuSpan();
				}
#endif
			}
#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
			else
			{
				flushGpuSpan();
			}
#endif
			gu += du;
			gv += dv;
		}
#if defined(OF_ECWOLF_OPENFPGA) && !defined(OF_PC)
		flushGpuSpan();
#endif
	}
}

// Textured Floor and Ceiling by DarkOne
// With multi-textured floors and ceilings stored in lower and upper bytes of
// according tile in third mapplane, respectively.
void DrawFloorAndCeiling(byte *vbuf, unsigned vbufPitch, int min_wallheight)
{
	const int halfheight = (viewheight >> 1) - viewshift;

	byte solidColor;
	if(R_GetUniformPlaneSolidColor(true, solidColor))
		R_DrawSolidPlane(vbuf, vbufPitch, min_wallheight, halfheight, viewz, solidColor);
	else
		R_DrawPlane(vbuf, vbufPitch, min_wallheight, halfheight, viewz);

	if(R_GetUniformPlaneSolidColor(false, solidColor))
		R_DrawSolidPlane(vbuf, vbufPitch, min_wallheight, halfheight, viewz+(map->GetPlane(0).depth<<FRACBITS), solidColor);
	else
		R_DrawPlane(vbuf, vbufPitch, min_wallheight, halfheight, viewz+(map->GetPlane(0).depth<<FRACBITS));
}
