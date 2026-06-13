// jm_tp.cpp
//
// JAM TEXT PRESENTER -- faithful port from bstone (src/jm_tp.cpp) to the
// Blake Stone ECWolf port.
//
// Original presenter: Michael D. Maynard, Copyright 1993 JAM Productions, Inc.
// bstone reimplementation: Copyright (c) 2013-2024 Boris I. Bendovsky et al.
// SPDX-License-Identifier: GPL-2.0-or-later
//
// All control-code parsing and text layout is the real engine logic.  Only the
// lowest-level draw / measure / input primitives are adapted to the host:
//
//   * fontnumber -> FFont* via tpFont(): Blake VGAGRAPH fonts.
//   * USL_DrawString(str): VWB_DrawPropString(font, str, CR_UNTRANSLATED,
//       stencil=true, stencilcolor=fontcolor) at logical (px,py); the host
//       maps logical 320x200 -> surface internally (VirtualToRealCoords),
//       exactly as blake_sbar.cpp does.  px advances by the measured width.
//   * VWL_MeasureString -> VW_MeasurePropString(tpFont(fontnumber), ...).
//   * VWB_Bar(x,y,w,h,color) -> VirtualToRealCoords + VWB_Clear, mirroring
//       wl_inter.cpp's BlakeBar.
//   * Shapes (^SH/^AN/^BX): TP_DrawShape / TP_AnimatePage / TP_BoxAroundShape
//       are stubbed so they parse-and-skip; text still flows.
//   * Graphics caching (CA_CacheGrChunk / grsegs / TP_CacheIn / TP_PurgeAllGfx)
//       is a no-op: ECWolf auto-caches and fonts come from FFont.  Script text
//       comes from a VGAGRAPH lump (Wads.ReadLump), not a DOS file.

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "wl_def.h"
#include "id_in.h"
#include "id_vh.h"
#include "id_vl.h"
#include "v_font.h"
#include "v_palette.h"
#include "v_video.h"
#include "w_wad.h"
#include "wl_play.h"

#include "jm_tp.h"

// -------------------------------------------------------------------------
// Host primitive shim
// -------------------------------------------------------------------------
//
// The presenter draws into a logical 320x200 space.  px/py are logical
// coordinates; the host's VWB_* routines map them to the real surface via
// VirtualToRealCoords(...,320,200,...), matching blake_sbar / wl_inter.
//
// id_vh.cpp exposes int px, py globals already; the presenter uses its own.

// fontcolor is a raw BLAKEPAL palette index (the script's ^FC value).
static uint8_t fontcolor;
static int8_t fontnumber;

// Map a Blake VGAGRAPH font number to a host FFont.
//   0 = FONT0SHF  (small index/terminal font)
//   1 = BIGFONT
//   2 = SMALLFNT
//   3 = FONTHUGE
//   4 = FONTMED   (the briefing body font; pi.fontnumber == 4)
//
// SmallFont / BigFont are engine globals; the rest are fetched by name.  Any
// font that fails to resolve falls back to SmallFont so the presenter never
// dereferences null.
static FFont* tpFont(int n)
{
	static FFont* cache[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
	static const char* const names[5] = {
		"FONT0SHF", // 0
		nullptr,    // 1 -> BigFont
		nullptr,    // 2 -> SmallFont
		"FONTHUGE", // 3
		"FONTMED",  // 4
	};

	if (n < 0 || n > 4)
	{
		n = 2;
	}

	if (!cache[n])
	{
		if (n == 1)
		{
			cache[n] = BigFont;
		}
		else if (n == 2)
		{
			cache[n] = SmallFont;
		}
		else
		{
			cache[n] = V_GetFont(names[n]);
		}

		if (!cache[n])
		{
			cache[n] = SmallFont;
		}
	}

	return cache[n];
}

// ch_width / font_height read from the active host font.
#define ch_width(ch) (tpFont(fontnumber)->GetCharWidth(static_cast<unsigned char>(ch)))
#define font_height (tpFont(fontnumber)->GetHeight())
#define is_shadowed ((flags & fl_shadowtext) == fl_shadowtext)

// Draw a string at logical (px,py) with the active font, stencil-filled to the
// raw palette index in fontcolor; advance px past it.  This is the host
// analogue of bstone's USL_DrawString (which marks blocks + draws raw-index
// text).  The host maps logical->surface; we do not assume 1:1.
static void USL_DrawString(const char* string)
{
	::px = px;
	::py = py;
	// Draw at the logical (px,py) via the same 320x200 VirtualToRealCoords
	// mapping the bars use; default pa==MENU_CENTER would offset/hide the text.
	pa = MENU_NONE;

	VWB_DrawPropString(
		tpFont(fontnumber),
		string,
		CR_UNTRANSLATED,
		true,
		fontcolor);

	word w, h;
	VW_MeasurePropString(tpFont(fontnumber), string, w, h);
	px += w;
}

// VWL_MeasureString -> host measurement.  bstone signature is
// (str, &width, &height, font); width/height are int.
static void VWL_MeasureString(
	const char* string,
	int* width,
	int* height,
	FFont* font)
{
	word w, h;
	VW_MeasurePropString(font, string, w, h);
	*width = w;
	*height = h;
}

// VWB_Bar(x,y,w,h,color): fill a logical rectangle.  Mirror wl_inter.cpp's
// BlakeBar: pass the logical rect through VirtualToRealCoords, then VWB_Clear
// with surface coords.
static void VWB_Bar(int x, int y, int w, int h, uint8_t color)
{
	double dx = x, dy = y, dw = w, dh = h;
	screen->VirtualToRealCoords(dx, dy, dw, dh, 320, 200, true, true);
	VWB_Clear(color, dx, dy, dx + dw, dy + dh);
}

// Host has no VGA palette cycling; the presenter calls CycleColors during
// pauses purely cosmetically.  No-op.
static inline void CycleColors() {}

// -------------------------------------------------------------------------
// ShPrint -- shadowed print (3d_inter.cpp:55).  Draws the string offset by
// (1,1) in shadow_color, then again at (px,py) in fontcolor.
// -------------------------------------------------------------------------
static void ShPrint(
	const char* text,
	int8_t shadow_color,
	bool single_char)
{
	uint16_t old_color = fontcolor, old_x = px, old_y = py;
	const char* string;
	char buf[2] = {0, 0};

	if (single_char)
	{
		string = buf;
		buf[0] = *text;
	}
	else
	{
		string = text;
	}

	fontcolor = static_cast<uint8_t>(shadow_color);
	py++;
	px++;
	USL_DrawString(string);

	fontcolor = static_cast<uint8_t>(old_color);
	py = old_y;
	px = old_x;
	USL_DrawString(string);
}

// -------------------------------------------------------------------------
// Presenter globals (file-static, mirroring bstone exactly).
// -------------------------------------------------------------------------
static int8_t old_fontnumber;

enum JustifyMode
{
	jm_left,
	jm_right,
	jm_flush
}; // JustifyMode

static int8_t justify_mode = jm_left;

static uint16_t flags;

static int16_t bgcolor, ltcolor, dkcolor, shcolor, anim_bgcolor = -1;
static int16_t xl;
static int16_t yl;
static int16_t xh;
static int16_t yh;
static int16_t cur_x;
static int16_t cur_y;
static int16_t last_cur_x;
static int16_t last_cur_y;
static const char* first_ch;

static const char* scan_ch;
static int16_t scan_x;
static int16_t numanims;
static int16_t stemp;

static PresenterInfo* pi;

static int16_t save_cx[TP_CURSOR_SAVES + 1] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
static int16_t save_cy[TP_CURSOR_SAVES + 1] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
static int16_t pagex[2], pagey[2];

static bool tp_handle_codes_can_peek_prev_2 = false;
static const char* first_ch_copy = nullptr;

int TPscan;

// -------------------------------------------------------------------------
// Page-advance input wait.  Reads the host control state and fills a small
// directional struct used by the ^EP loop, matching bstone's ReadAnyControl.
// -------------------------------------------------------------------------
struct TPControl
{
	bool button0; // accept / continue
	bool button1; // abort
	Direction dir;
};

static void TP_ReadControl(TPControl* ci)
{
	IN_ProcessEvents();

	ci->button0 = false;
	ci->button1 = false;
	ci->dir = dir_None;

	if (Keyboard[sc_Space] || Keyboard[sc_Return] || Keyboard[sc_Enter])
	{
		ci->button0 = true;
	}

	if (Keyboard[sc_Escape])
	{
		ci->button1 = true;
	}

	if (Keyboard[sc_UpArrow] || Keyboard[sc_PgUp])
	{
		ci->dir = dir_North;
	}
	else if (Keyboard[sc_DownArrow] || Keyboard[sc_PgDn])
	{
		ci->dir = dir_South;
	}
	else if (Keyboard[sc_LeftArrow])
	{
		ci->dir = dir_West;
	}
	else if (Keyboard[sc_RightArrow])
	{
		ci->dir = dir_East;
	}
}

// -------------------------------------------------------------------------
// TP_Presenter -- main entry.  Faithful port of bstone TP_Presenter.
// -------------------------------------------------------------------------
void TP_Presenter(
	PresenterInfo* pinfo)
{
	pi = pinfo;
	bgcolor = pi->bgcolor;
	ltcolor = pi->ltcolor;
	dkcolor = pi->dkcolor;
	shcolor = pi->shcolor;
	xl = pi->xl + TP_MARGIN;
	yl = pi->yl + TP_MARGIN;
	xh = pi->xh - TP_MARGIN;
	yh = pi->yh - TP_MARGIN;

	flags |= fl_clearscback;
	if ((pi->flags & TPF_USE_CURRENT) && (pi->cur_x != 0xffff) && (pi->cur_y != 0xffff))
	{
		if (pi->flags & TPF_SHOW_CURSOR)
		{
			cur_x = px;
			cur_y = py;
		}
		else
		{
			cur_x = pi->cur_x;
			cur_y = pi->cur_y;
		}
	}
	else
	{
		cur_x = xl;
		cur_y = yl;
	}
	first_ch = pi->script[0];
	first_ch_copy = first_ch;

	pi->pagenum = 0;
	numanims = 0;

	old_fontnumber = static_cast<int8_t>(fontnumber);
	fontnumber = pi->fontnumber;
	TP_PurgeAllGfx();
	TP_CachePage(first_ch);
	flags = fl_presenting | fl_startofline;
	if (*first_ch == TP_CONTROL_CHAR)
	{
		tp_handle_codes_can_peek_prev_2 = ((first_ch - first_ch_copy) >= 2);
		TP_HandleCodes();
	}

	// Display info UNDER defined region.
	//
	if (pi->infoline)
	{
		auto oldf = static_cast<int8_t>(fontnumber);
		auto oldc = fontcolor;

		px = xl;
		py = yh + TP_MARGIN + 1;
		fontnumber = 2;
		fontcolor = 0x39;
		VWB_Bar(xl - TP_MARGIN, py, xh - xl + 1 + (TP_MARGIN * 2), 8, static_cast<uint8_t>(bgcolor));
		ShPrint(pi->infoline, static_cast<int8_t>(shcolor), false);

		if (pi->flags & TPF_SHOW_PAGES)
		{
			px = 246;
			py = 190;
			ShPrint("PAGE ", static_cast<int8_t>(shcolor), false);
			pagex[0] = px;
			pagey[0] = py;
			ShPrint("   OF ", static_cast<int8_t>(shcolor), false);
			pagex[1] = px;
			pagey[1] = py;

			TP_PrintPageNumber();
		}

		fontcolor = oldc;
		fontnumber = oldf;
	}

	if (!(pi->flags & TPF_USE_CURRENT))
	{
		VWB_Bar(xl - TP_MARGIN, yl - TP_MARGIN, xh - xl + 1 + (TP_MARGIN * 2), yh - yl + 1 + (TP_MARGIN * 2), static_cast<uint8_t>(bgcolor));
	}

	if (pi->flags & TPF_SHOW_CURSOR)
	{
		px = cur_x;
		py = cur_y;
		TP_Print("@", true);
	}

	while (flags & fl_presenting)
	{
		if (*first_ch == TP_CONTROL_CHAR)
		{
			tp_handle_codes_can_peek_prev_2 = ((first_ch - first_ch_copy) >= 2);
			TP_HandleCodes();
		}
		else
		{
			TP_WrapText();
		}
	}

	fontnumber = old_fontnumber;
	pi->cur_x = cur_x;
	pi->cur_y = cur_y;

	if (pi->flags & TPF_SHOW_CURSOR)
	{
		cur_x = px = last_cur_x;
		cur_y = py = last_cur_y;
	}
	else
	{
		px = cur_x;
		py = cur_y;
	}

	pi->cur_x = cur_x;
	pi->cur_y = cur_y;
}

void TP_WrapText()
{
	char temp;

	flags &= ~fl_startofline;

	if ((stemp = TP_LineCommented(first_ch)) != 0)
	{
		first_ch += stemp;
		return;
	}

	// Parse script until one of the following:
	//
	// 1) text extends beyond right margin
	// 2) NULL termination is reached
	// 3) TP_RETURN_CHAR is reached
	// 4) TP_CONTROL_CHAR is reached
	//
	scan_x = cur_x;
	scan_ch = first_ch;
	while (((uint16_t)(scan_x) + (uint16_t)(ch_width(*scan_ch)) <= xh) && (*scan_ch) &&
		(*scan_ch != TP_RETURN_CHAR) && (*scan_ch != TP_CONTROL_CHAR))
	{
		scan_x += ch_width(*scan_ch++);
	}

	// If 'text extends beyond right margin', scan backwards for a SPACE
	//
	if ((uint16_t)scan_x + (uint16_t)(ch_width(*scan_ch)) > xh)
	{
		int16_t last_x = scan_x;
		const char* last_ch = scan_ch;

		while ((scan_ch != first_ch) && (*scan_ch != ' ') && (*scan_ch != TP_RETURN_CHAR))
		{
			scan_x -= ch_width(*scan_ch--);
		}

		if (scan_ch == first_ch)
		{
			if (cur_x != xl)
			{
				goto tp_newline;
			}

			scan_ch = last_ch;
			scan_x = last_x;
		}
	}

	// print current line
	//
	temp = *scan_ch;

	const_cast<char*>(scan_ch)[0] = '\0';

	if ((justify_mode == jm_right) && (!(flags & fl_center)))
	{
		int width, height;

		VWL_MeasureString(first_ch, &width, &height, tpFont(fontnumber));
		cur_x = static_cast<int16_t>(xh - width + 1);
		if (cur_x < xl)
		{
			cur_x = xl;
		}
	}

	px = cur_x;
	py = cur_y;

	if (*first_ch != TP_RETURN_CHAR)
	{
		if (pi->print_delay)
		{
			TP_SlowPrint(first_ch, pi->print_delay);
		}
		else
		{
			TP_Print(first_ch, false);
		}
	}

	const_cast<char*>(scan_ch)[0] = temp;

	first_ch = scan_ch;

tp_newline:;
	flags &= ~fl_center;

	// Skip SPACE at end of wrapped line.
	//
	if ((first_ch[0] == ' ') && (first_ch[1] != ' '))
	{
		first_ch++;
	}

	// Skip end-of-line designators
	//
	if (first_ch[0] == TP_RETURN_CHAR)
	{
		if (first_ch[1] == '\n')
		{
			first_ch += 2;
		}
		else
		{
			first_ch++;
		}
	}

	// TP_CONTROL_CHARs don't advance to next character line
	//
	if ((*scan_ch != TP_CONTROL_CHAR) && *scan_ch)
	{
		auto old_color = fontcolor;

		// Remove cursor.
		//
		if (pi->flags & TPF_SHOW_CURSOR)
		{
			fontcolor = static_cast<uint8_t>(bgcolor);
			px = last_cur_x;
			py = last_cur_y;
			TP_Print("@", true);
			fontcolor = old_color;
		}

		cur_x = xl;

		// If next line will be printed out of defined region, scroll up!
		// The host has no VL_ScreenToScreen scroll; clear and clamp instead.
		//
		if ((pi->flags & TPF_SCROLL_REGION) && (cur_y + (font_height * 2) > yh))
		{
			VWB_Bar(cur_x, cur_y, xh - xl + 1 + (TP_MARGIN * 2), yh - cur_y + 1, static_cast<uint8_t>(bgcolor));

			if (cur_y + font_height > yh)
			{
				cur_y = yh - font_height + 1 - is_shadowed;
			}
		}
		else
		{
			if (pi->custom_line_height > 0)
			{
				cur_y = static_cast<uint16_t>(cur_y + pi->custom_line_height + is_shadowed);
			}
			else
			{
				cur_y += font_height + is_shadowed;
			}
		}

		// Display cursor.
		//
		if (pi->flags & TPF_SHOW_CURSOR)
		{
			px = cur_x;
			py = cur_y;
			TP_Print("@", true);
		}
	}
}

void TP_HandleCodes()
{
	TPControl ci;
	bool ackReleased = false;  // require confirm/escape key release before a page advance
	uint16_t shapenum;
	int16_t length;
	const char* s;
	int8_t c;

	if (tp_handle_codes_can_peek_prev_2 &&
		(first_ch[-2] == TP_RETURN_CHAR) && (first_ch[-1] == '\n'))
	{
		flags |= fl_startofline;
	}

	while (*first_ch == TP_CONTROL_CHAR)
	{
		const char* const TP_MORE_TEXT = "<MORE>";

		char temp;

		first_ch++;
#ifndef TP_CASE_SENSITIVE
		*const_cast<char*>(first_ch) = toupper(*first_ch);
		*const_cast<char*>(first_ch + 1) = toupper(*(first_ch + 1));
#endif
		uint16_t subcode;
		// Byte-wise to avoid an unaligned 16-bit load on strict-alignment hosts.
		uint16_t code = static_cast<uint8_t>(first_ch[0]) |
			(static_cast<uint8_t>(first_ch[1]) << 8);
		first_ch += 2;

		switch (code)
		{
			// CENTER TEXT ------------------------------------------------------
			//
		case TP_CNVT_CODE('C', 'E'):
			length = 0;
			s = first_ch;
			while (*s && (*s != TP_RETURN_CHAR))
			{
				switch (*s)
				{
				case TP_CONTROL_CHAR:
					s++;
					subcode = static_cast<uint8_t>(s[0]) |
						(static_cast<uint8_t>(s[1]) << 8);
					s += 2;
					switch (subcode)
					{
					case TP_CNVT_CODE('S', 'X'):
					case TP_CNVT_CODE('R', 'X'):
					case TP_CNVT_CODE('S', 'Y'):
					case TP_CNVT_CODE('R', 'Y'):
					case TP_CNVT_CODE('F', 'N'):
					case TP_CNVT_CODE('S', 'T'):
					case TP_CNVT_CODE('B', 'X'):
					case TP_CNVT_CODE('S', 'P'):
						s++;
						break;

					case TP_CNVT_CODE('F', 'C'):
					case TP_CNVT_CODE('B', 'C'):
					case TP_CNVT_CODE('S', 'C'):
					case TP_CNVT_CODE('L', 'C'):
					case TP_CNVT_CODE('D', 'C'):
					case TP_CNVT_CODE('A', 'X'):
					case TP_CNVT_CODE('A', 'Y'):
					case TP_CNVT_CODE('H', 'C'):
						s += 2;
						break;

					case TP_CNVT_CODE('L', 'M'):
					case TP_CNVT_CODE('R', 'M'):
					case TP_CNVT_CODE('P', 'X'):
					case TP_CNVT_CODE('P', 'Y'):
						s += 3;
						break;

					case TP_CNVT_CODE('S', 'H'):
						// Shapes stubbed: width 0 so centering ignores them.
						s += 3;
						length += TP_BoxAroundShape(-1, -1, 0, pis_pic);
						break;

					case TP_CNVT_CODE('A', 'N'):
						s += 2;
						length += TP_BoxAroundShape(-1, -1, 0, pis_pic);
						break;

					case TP_CNVT_CODE('Z', 'Z'):
					case TP_CNVT_CODE('D', 'M'):
					case TP_CNVT_CODE('C', 'E'):
					case TP_CNVT_CODE('E', 'P'):
					case TP_CNVT_CODE('L', 'J'):
					case TP_CNVT_CODE('R', 'J'):
					case TP_CNVT_CODE('X', 'X'):
					case TP_CNVT_CODE('S', 'L'):
					case TP_CNVT_CODE('R', 'L'):
					case TP_CNVT_CODE('B', 'E'):
					case TP_CNVT_CODE('H', 'I'):
					case TP_CNVT_CODE('P', 'A'):
					case TP_CNVT_CODE('M', 'O'):
					case TP_CNVT_CODE('H', 'O'):
					case TP_CNVT_CODE('H', 'F'):
					case TP_CNVT_CODE('S', 'B'):
						// No parameters to pass over!
						break;
					}
					break;

				default:
					length += ch_width(*s++);
					break;
				}
			}
			cur_x += (uint16_t)((xh - cur_x + 1) - length) / 2;
			flags |= fl_center;

			if (pi->flags & TPF_SHOW_CURSOR)
			{
				TP_JumpCursor();
			}
			break;

			// DRAW SHAPE -------------------------------------------------------
			//
		case TP_CNVT_CODE('S', 'H'):
			shapenum = TP_VALUE(first_ch, 3);
			first_ch += 3;
			TP_DrawShape(cur_x, cur_y, shapenum, pis_pic);
			break;

			// CLEAR SCALED BACKGROUND -------------------------------------------
			//
		case TP_CNVT_CODE('S', 'B'):
			if (TP_VALUE(first_ch++, 1))
			{
				flags |= fl_clearscback;
			}
			else
			{
				flags &= ~fl_clearscback;
			}
			break;

			// HIGHLIGHT COLOR ---------------------------------------------------
			//
		case TP_CNVT_CODE('H', 'C'):
			pi->highlight_color = static_cast<uint8_t>(TP_VALUE(first_ch, 2));
			first_ch += 2;
			break;

			// HIGHLIGHT ON ------------------------------------------------------
			//
		case TP_CNVT_CODE('H', 'O'):
			pi->fontcolor = fontcolor;
			fontcolor = pi->highlight_color;
			break;

			// HIGHLIGHT OFF -----------------------------------------------------
			//
		case TP_CNVT_CODE('H', 'F'):
			fontcolor = pi->fontcolor;
			break;

			// ALTER X ----------------------------------------------------------
			//
		case TP_CNVT_CODE('A', 'X'):
			c = static_cast<int8_t>(TP_VALUE(first_ch, 2));
			first_ch += 2;
			cur_x += c;
			break;

			// ALTER Y ----------------------------------------------------------
			//
		case TP_CNVT_CODE('A', 'Y'):
			c = static_cast<int8_t>(TP_VALUE(first_ch, 2));
			first_ch += 2;
			cur_y += c;
			break;

			// INIT ANIMATION ---------------------------------------------------
			// Shapes stubbed: consume the arg, no anim registered.
			//
		case TP_CNVT_CODE('A', 'N'):
			first_ch += 2;
			break;

			// FONT COLOR -------------------------------------------------------
			//
		case TP_CNVT_CODE('F', 'C'):
			fontcolor = static_cast<uint8_t>(TP_VALUE(first_ch, 2));
			first_ch += 2;
			break;

			// SHADOW COLOR ------------------------------------------------------
			//
		case TP_CNVT_CODE('S', 'C'):
			shcolor = TP_VALUE(first_ch, 2);
			first_ch += 2;
			break;

			// LIGHT COLOR -------------------------------------------------------
			//
		case TP_CNVT_CODE('L', 'C'):
			ltcolor = TP_VALUE(first_ch, 2);
			first_ch += 2;
			break;

			// SAVE LOCATION -----------------------------------------------------
			//
		case TP_CNVT_CODE('S', 'L'):
			save_cx[TP_CURSOR_SAVES] = cur_x;
			save_cy[TP_CURSOR_SAVES] = cur_y;
			break;

			// RESTORE LOCATION --------------------------------------------------
			//
		case TP_CNVT_CODE('R', 'L'):
			cur_x = save_cx[TP_CURSOR_SAVES];
			cur_y = save_cy[TP_CURSOR_SAVES];

			if (pi->flags & TPF_SHOW_CURSOR)
			{
				TP_JumpCursor();
			}
			break;

			// SAVE X LOCATION ---------------------------------------------------
			//
		case TP_CNVT_CODE('S', 'X'):
			temp = static_cast<char>(TP_VALUE(first_ch++, 1));
			if (pi->flags & TPF_SHOW_CURSOR)
			{
				save_cx[static_cast<int>(temp)] = last_cur_x;
			}
			else
			{
				save_cx[static_cast<int>(temp)] = cur_x;
			}
			break;

			// RESTORE X LOCATION ------------------------------------------------
			//
		case TP_CNVT_CODE('R', 'X'):
			temp = static_cast<char>(TP_VALUE(first_ch++, 1));
			cur_x = save_cx[static_cast<int>(temp)];

			if (pi->flags & TPF_SHOW_CURSOR)
			{
				TP_JumpCursor();
			}
			break;

			// SAVE Y LOCATION ---------------------------------------------------
			//
		case TP_CNVT_CODE('S', 'Y'):
			temp = static_cast<char>(TP_VALUE(first_ch++, 1));
			if (pi->flags & TPF_SHOW_CURSOR)
			{
				save_cy[static_cast<int>(temp)] = last_cur_y;
			}
			else
			{
				save_cy[static_cast<int>(temp)] = cur_y;
			}
			break;

			// RESTORE Y LOCATION ------------------------------------------------
			//
		case TP_CNVT_CODE('R', 'Y'):
			temp = static_cast<char>(TP_VALUE(first_ch++, 1));
			cur_y = save_cy[static_cast<int>(temp)];

			if (pi->flags & TPF_SHOW_CURSOR)
			{
				TP_JumpCursor();
			}
			break;

			// NOP ---------------------------------------------------------------
			//
		case TP_CNVT_CODE('Z', 'Z'):
			break;

			// DARK COLOR --------------------------------------------------------
			//
		case TP_CNVT_CODE('D', 'C'):
			dkcolor = TP_VALUE(first_ch, 2);
			first_ch += 2;
			break;

			// FONT NUMBER -------------------------------------------------------
			// Host fonts are auto-cached via tpFont(); just set the number.
			//
		case TP_CNVT_CODE('F', 'N'):
			fontnumber = TP_VALUE(first_ch++, 1);
			break;

			// BACKGROUND COLOR -------------------------------------------------
			//
		case TP_CNVT_CODE('B', 'C'):
			bgcolor = TP_VALUE(first_ch, 2);
			first_ch += 2;
			break;

			// SHADOW TEXT ------------------------------------------------------
			//
		case TP_CNVT_CODE('S', 'T'):
			if (TP_VALUE(first_ch++, 1))
			{
				flags |= fl_shadowtext;
			}
			else
			{
				flags &= ~fl_shadowtext;
			}
			break;

			// SHADOW PIC -------------------------------------------------------
			//
		case TP_CNVT_CODE('S', 'P'):
			if (TP_VALUE(first_ch++, 1))
			{
				flags |= fl_shadowpic;
			}
			else
			{
				flags &= ~fl_shadowpic;
			}
			break;

			// BOX SHAPES -------------------------------------------------------
			//
		case TP_CNVT_CODE('B', 'X'):
			if (TP_VALUE(first_ch++, 1))
			{
				flags |= fl_boxshape;
			}
			else
			{
				flags &= ~fl_boxshape;
			}
			break;

			// LEFT MARGIN ------------------------------------------------------
			//
		case TP_CNVT_CODE('L', 'M'):
			shapenum = TP_VALUE(first_ch, 3);
			first_ch += 3;
			if (shapenum == 0xfff)
			{
				xl = cur_x;
			}
			else
			{
				xl = shapenum;
			}
			if (cur_x < xl)
			{
				cur_x = xl;
			}
			break;

			// RIGHT MARGIN -----------------------------------------------------
			//
		case TP_CNVT_CODE('R', 'M'):
			shapenum = TP_VALUE(first_ch, 3);
			first_ch += 3;
			if (shapenum == 0xfff)
			{
				xh = cur_x;
			}
			else
			{
				xh = shapenum;
			}
			break;

			// DEFAULT MARGINS --------------------------------------------------
			//
		case TP_CNVT_CODE('D', 'M'):
			xl = pi->xl + TP_MARGIN;
			yl = pi->yl + TP_MARGIN;
			xh = pi->xh - TP_MARGIN;
			yh = pi->yh - TP_MARGIN;
			break;

			// SET X COORDINATE -------------------------------------------------
			//
		case TP_CNVT_CODE('P', 'X'):
			cur_x = TP_VALUE(first_ch, 3);
			first_ch += 3;

			if (pi->flags & TPF_SHOW_CURSOR)
			{
				TP_JumpCursor();
			}
			break;

			// SET Y COORDINATE -------------------------------------------------
			//
		case TP_CNVT_CODE('P', 'Y'):
			cur_y = TP_VALUE(first_ch, 3);
			first_ch += 3;

			if (pi->flags & TPF_SHOW_CURSOR)
			{
				TP_JumpCursor();
			}
			break;

			// LEFT JUSTIFY -----------------------------------------------------
			//
		case TP_CNVT_CODE('L', 'J'):
			justify_mode = jm_left;
			break;

			// RIGHT JUSTIFY ----------------------------------------------------
			//
		case TP_CNVT_CODE('R', 'J'):
			justify_mode = jm_right;
			break;

			// BELL -------------------------------------------------------------
			// No terminal beep sound wired up for briefings; ignore.
			//
		case TP_CNVT_CODE('B', 'E'):
			break;

			// HIDE CURSOR ------------------------------------------------------
			//
		case TP_CNVT_CODE('H', 'I'):
			break;

			// PAUSE -----------------------------------------------------------
			//
		case TP_CNVT_CODE('P', 'A'):
		{
			int8_t i;

			for (i = 0; i < 30; i++)
			{
				VW_WaitVBL(1);
				CycleColors();
			}
		}
		break;

		// MORE ------------------------------------------------------------
		//
		case TP_CNVT_CODE('M', 'O'):
			if (pi->print_delay)
			{
				TP_SlowPrint(TP_MORE_TEXT, pi->print_delay);
			}
			else
			{
				TP_Print(TP_MORE_TEXT, false);
			}

			LastScan = sc_None;
			do
			{
				TP_ReadControl(&ci);
			} while (!ci.button0 && !ci.button1 && (ci.dir == dir_None) &&
				(LastScan == sc_None));

			cur_x = xl;
			VWB_Bar(cur_x, cur_y, xh - xl + 1 + (TP_MARGIN * 2), font_height + is_shadowed, static_cast<uint8_t>(bgcolor));
			if (pi->flags & TPF_SHOW_CURSOR)
			{
				TP_JumpCursor();
			}

			if (LastScan == sc_Escape)
			{
				flags &= ~fl_presenting;
			}
			TPscan = LastScan;
			break;

			// DISPLAY STRING --------------------------------------------------
			// Display string table is unused for briefings; consume the arg.
			//
		case TP_CNVT_CODE('D', 'S'):
			first_ch += 2;
			break;

			// PLAY MUSIC -------------------------------------------------------
			// No presenter music wired up; consume the arg.
			//
		case TP_CNVT_CODE('P', 'M'):
			first_ch += 2;
			break;

			// PLAY SOUND -------------------------------------------------------
			// No presenter sound wired up; consume the arg.
			//
		case TP_CNVT_CODE('P', 'S'):
			first_ch += 2;
			break;

			// END OF PAGE ------------------------------------------------------
			//
		case TP_CNVT_CODE('E', 'P'):
			VW_UpdateScreen();

			// The presenter is entered with the palette faded to black; reveal
			// the first drawn page (bstone jm_tp.cpp:2878).
			if (screenfaded)
				VW_FadeIn();

			// The confirm/escape key that opened the briefing (difficulty
			// select) is still held when we get here; require a release before
			// accepting a press so the page isn't dismissed on the same tap.
			ackReleased = false;

			while (true)
			{
				CycleColors();
				CalcTics();

				TP_AnimatePage(numanims);
				VW_UpdateScreen();
				TP_ReadControl(&ci);

				if (!ci.button0 && !ci.button1)
					ackReleased = true;

				if (ackReleased && pi->flags & TPF_CONTINUE && (ci.button0))
				{
					flags &= ~fl_presenting;
					break;
				}

				if (ackReleased && ci.button1)
				{
					flags &= ~fl_presenting;
					TPscan = sc_Escape;
					break;
				}
				else
				{
					if (((ci.dir == dir_North) || (ci.dir == dir_West)) && (pi->pagenum))
					{
						if (flags & fl_upreleased)
						{
							pi->pagenum--;
							flags &= ~fl_upreleased;
							break;
						}
					}
					else
					{
						flags |= fl_upreleased;
						if (((ci.dir == dir_South) || (ci.dir == dir_East)) && (pi->pagenum < pi->numpages - 1))
						{
							if (flags & fl_dnreleased)
							{
								pi->pagenum++;
								flags &= ~fl_dnreleased;
								break;
							}
						}
						else
						{
							flags |= fl_dnreleased;
						}
					}
				}

				VW_WaitVBL(1);
			}

			cur_x = xl;
			cur_y = yl;
			if (cur_y + font_height > yh)
			{
				cur_y = yh - font_height;
			}
			first_ch = pi->script[static_cast<int>(pi->pagenum)];
			first_ch_copy = first_ch;

			numanims = 0;
			TP_PurgeAllGfx();
			TP_CachePage(first_ch);

			if (*first_ch == TP_CONTROL_CHAR)
			{
				tp_handle_codes_can_peek_prev_2 = ((first_ch - first_ch_copy) >= 2);
				TP_HandleCodes();
				flags &= ~fl_startofline;
			}
			VWB_Bar(xl, yl, xh - xl + 1, yh - yl + 1, static_cast<uint8_t>(bgcolor));
			TP_PrintPageNumber();
			break;

			// EXIT PRESENTER ---------------------------------------------------
			//
		case TP_CNVT_CODE('X', 'X'):
			flags &= ~fl_presenting;
			VW_UpdateScreen();
			break;
		}
	}

	if ((first_ch[0] == TP_RETURN_CHAR) && (first_ch[1] == '\n') && (flags & fl_startofline))
	{
		first_ch += 2;
	}
}

void TP_PrintPageNumber()
{
	auto oldf = static_cast<int8_t>(fontnumber);
	auto oldc = fontcolor;

	if (!(pi->flags & TPF_SHOW_PAGES))
	{
		return;
	}

	fontnumber = 2;
	fontcolor = 0x39;

	// Print current page number.
	//
	px = pagex[0];
	py = pagey[0];
	VWB_Bar(px, py, 12, 7, 0xe3);

	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%2d", pi->pagenum + 1);

	ShPrint(buffer, static_cast<int8_t>(shcolor), false);

	// Print total page count.
	//
	if ((px = pagex[1]) > -1)
	{
		py = pagey[1];

		snprintf(buffer, sizeof(buffer), "%2d", pi->numpages);

		ShPrint(buffer, static_cast<int8_t>(shcolor), false);

		pagex[1] = -1;
	}

	fontnumber = oldf;
	fontcolor = oldc;
}

// -------------------------------------------------------------------------
// Shape stubs.  TODO(blake-shapes): port piShapeTable sprite/pic embedding
// (next faithful stage).  Until then ^SH/^AN/^BX parse-and-skip with zero
// footprint so text continues to flow.
// -------------------------------------------------------------------------
int16_t TP_DrawShape(
	int16_t x,
	int16_t y,
	int16_t shapenum,
	pisType shapetype)
{
	(void)x;
	(void)y;
	(void)shapenum;
	(void)shapetype;
	// TODO(blake-shapes): port piShapeTable sprite/pic embedding (next faithful stage).
	return 0;
}

void TP_AnimatePage(
	int16_t num_anims)
{
	(void)num_anims;
	// TODO(blake-shapes): port piShapeTable sprite/pic embedding (next faithful stage).
}

int16_t TP_BoxAroundShape(
	int16_t x1,
	int16_t y1,
	uint16_t shapenum,
	pisType shapetype)
{
	(void)x1;
	(void)y1;
	(void)shapenum;
	(void)shapetype;
	// TODO(blake-shapes): port piShapeTable sprite/pic embedding (next faithful stage).
	return 0;
}

void TP_PurgeAllGfx()
{
	// ECWolf auto-caches graphics; nothing to purge.
}

void TP_CachePage(
	const char* script)
{
	// ECWolf auto-caches; fonts come from FFont via tpFont().  Nothing to
	// pre-cache.  Faithful page scan happens in TP_InitScript.
	(void)script;
}

uint16_t TP_VALUE(
	const char* ptr,
	int8_t num_nybbles)
{
	char ch;
	int8_t nybble;
	int8_t shift;
	uint16_t value = 0;

	for (nybble = 0; nybble < num_nybbles; nybble++)
	{
		shift = 4 * (num_nybbles - nybble - 1);

		ch = *ptr++;
		if (isxdigit(static_cast<unsigned char>(ch)))
		{
			if (isalpha(static_cast<unsigned char>(ch)))
			{
				value |= (toupper(ch) - 'A' + 10) << shift;
			}
			else
			{
				value |= (ch - '0') << shift;
			}
		}
	}

	return value;
}

void TP_JumpCursor()
{
	auto old_color = fontcolor;

	fontcolor = static_cast<uint8_t>(bgcolor);
	px = last_cur_x;
	py = last_cur_y;
	TP_Print("@", true);
	px = cur_x = last_cur_x;
	py = cur_y = last_cur_y;
	fontcolor = old_color;
	TP_Print("@", true);
}

void TP_Print(
	const char* string,
	bool single_char)
{
	LastScan = sc_None;

	last_cur_x = cur_x;
	last_cur_y = cur_y;

	if ((flags & fl_shadowtext) && (*string != '@'))
	{
		if (fontcolor == bgcolor)
		{
			ShPrint(string, static_cast<int8_t>(bgcolor), single_char);
		}
		else
		{
			ShPrint(string, static_cast<int8_t>(shcolor), single_char);
		}
	}
	else if (single_char)
	{
		char buf[2] = {0, 0};

		buf[0] = *string;
		USL_DrawString(buf);
	}
	else
	{
		USL_DrawString(string);
	}

	cur_x = px;
	cur_y = py;

	if ((pi->flags & TPF_ABORTABLE) && LastScan != sc_None)
	{
		flags &= ~fl_presenting;
	}
}

bool TP_SlowPrint(
	const char* string,
	int8_t delay)
{
	auto old_color = fontcolor;
	int16_t old_x, old_y;
	int32_t tc;
	bool aborted = false;

	while (*string)
	{
		if (pi->flags & TPF_SHOW_CURSOR)
		{
			// Remove the cursor.
			//
			fontcolor = static_cast<uint8_t>(bgcolor);
			px = old_x = last_cur_x;
			py = old_y = last_cur_y;
			TP_Print("@", true);
			px = old_x;
			py = old_y;
			fontcolor = old_color;
		}

		// If user aborted, print the whole string ...
		// Otherwise, just print a character ...
		//
		if (aborted)
		{
			TP_Print(string, false);
		}
		else
		{
			TP_Print(string++, true);
		}

		// Print cursor
		//
		if (pi->flags & TPF_SHOW_CURSOR)
		{
			TP_Print("@", true);
		}

		VW_UpdateScreen();

		// Break out on abort!
		//
		if (aborted)
		{
			break;
		}

		// Delay and check for abort (if needed).
		//
		if (!aborted)
		{
			LastScan = sc_None;
			tc = GetTimeCount();
			while (GetTimeCount() - tc < delay)
			{
				VW_WaitVBL(1);
				IN_ProcessEvents();
				CycleColors();
				if (pi->flags & TPF_ABORTABLE)
				{
					if (LastScan != sc_None)
					{
						aborted = true;
						break;
					}
				}
			}
		}
	}

	if (aborted)
	{
		flags &= ~fl_presenting;
	}

	return aborted;
}

// -------------------------------------------------------------------------
// Script loading.  Source is a VGAGRAPH text lump (already located by the
// caller), copied NUL-terminated into a presenter-owned buffer.
// -------------------------------------------------------------------------
int32_t TP_LoadScript(
	int lumpnum,
	PresenterInfo* p_i)
{
	if (lumpnum < 0)
	{
		p_i->script[0] = nullptr;
		p_i->scriptstart = nullptr;
		return -1;
	}

	FMemLump lump = Wads.ReadLump(lumpnum);
	const char* data = reinterpret_cast<const char*>(lump.GetMem());
	int32_t lumpsize = static_cast<int32_t>(lump.GetSize());

	// Copy the lump bytes into a writable, NUL-terminated buffer.  The parser
	// writes terminators back into the script (line wrapping), so it must be
	// mutable and presenter-owned.
	char* buffer = new char[lumpsize + 1];
	memcpy(buffer, data, lumpsize);
	buffer[lumpsize] = '\0';

	p_i->id_cache = -1;
	p_i->scriptstart = buffer;

	const char* p = strstr(buffer, "^XX");
	int32_t size;

	if (!p)
	{
		// No terminator: present the whole lump as one page.
		size = lumpsize;
	}
	else
	{
		size = static_cast<int32_t>(p - buffer);
	}

	p_i->script[0] = buffer;
	p_i->flags |= TPF_CACHED_SCRIPT;
	TP_InitScript(p_i);

	return size;
}

void TP_FreeScript(
	PresenterInfo* p_i)
{
	TP_PurgeAllGfx();

	if (p_i->scriptstart)
	{
		delete[] static_cast<char*>(p_i->scriptstart);
		p_i->scriptstart = nullptr;
	}
	p_i->script[0] = nullptr;
}

void TP_InitScript(
	PresenterInfo* p_i)
{
	const char* script = p_i->script[0];
	uint16_t code;

	p_i->numpages = 1; // Assume at least 1 page
	while (*script)
	{
		while ((stemp = TP_LineCommented(script)) != 0)
		{
			script += stemp;
			if (!*script)
			{
				goto end_func;
			}
		}

		switch (*script++)
		{
		case TP_CONTROL_CHAR:
			code = static_cast<uint8_t>(script[0]) |
				(static_cast<uint8_t>(script[1]) << 8);
			script += 2;
			switch (code)
			{
			case TP_CNVT_CODE('E', 'P'):
				if (p_i->numpages < TP_MAX_PAGES)
				{
					p_i->script[static_cast<int>(p_i->numpages++)] = script;
				}
				break;
			}
			break;
		}
	}

end_func:;
	p_i->numpages--; // Last page defined is not a real page.
}

int16_t TP_LineCommented(
	const char* s)
{
	const char* o = s;

	// If a line starts with a semi-colon, the entire line is considered a
	// comment and is ignored!
	//
	if ((*s == ';') && (*(s - 2) == TP_RETURN_CHAR))
	{
		while (*s != TP_RETURN_CHAR)
		{
			s++;
		}
		s += 2;
	}

	return static_cast<int8_t>(s - o);
}
