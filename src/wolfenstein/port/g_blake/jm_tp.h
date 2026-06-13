// jm_tp.h
//
// JAM Text Presenter -- ported from bstone (src/jm_tp.h, jm_tp.cpp) for the
// Blake Stone ECWolf port.  Renders Blake mission briefings faithfully from
// the original ^-code script language.
//
// Original presenter: Michael D. Maynard, Copyright 1993 JAM Productions, Inc.
// bstone reimplementation: Copyright (c) 2013-2024 Boris I. Bendovsky et al.
//
// Only the lowest-level draw/measure/input primitives are adapted to the host
// engine (see jm_tp.cpp).  All text layout / control-code logic is the real
// engine logic.

#ifndef __BLAKE_JM_TP_H__
#define __BLAKE_JM_TP_H__

#include <stdint.h>

// -------------------------------------------------------------------------
// defines
// -------------------------------------------------------------------------

#define PI_MAX_NUM_DISP_STRS 1 // Num display str ptrs allocated for text presenter

#define TP_CASE_SENSITIVE // ctrl codes are case sensitive

#define TP_640x200 0 // is presenter in 640 x 200 mode?

#define TP_RETURN_CHAR '\r'
#define TP_CONTROL_CHAR '^'

#define TP_CURSOR_SAVES 8 // MAX different points to save

#define TP_CNVT_CODE(c1, c2) ((c1) | (c2 << 8))

#define TP_MAX_ANIMS 10
#define TP_MAX_PAGES 41

#define TP_MARGIN 1 // distance between xl/xh/yl/yh points and text

//
// global static flags
//

#define fl_center 0x0001
#define fl_uncachefont 0x0002
#define fl_boxshape 0x0004
#define fl_shadowtext 0x0008
#define fl_presenting 0x0010
#define fl_startofline 0x0020
#define fl_upreleased 0x0040
#define fl_dnreleased 0x0080
#define fl_pagemanager 0x0100
#define fl_hidecursor 0x0200
#define fl_shadowpic 0x0400
#define fl_clearscback 0x0800

//
// PresenterInfo structure flags
//

#define TPF_CACHED_SCRIPT 0x0001
#define TPF_CACHE_NO_GFX 0x0002
#define TPF_CONTINUE 0x0004
#define TPF_USE_CURRENT 0x0008
#define TPF_SHOW_CURSOR 0x0010
#define TPF_SCROLL_REGION 0x0020
#define TPF_SHOW_PAGES 0x0040
#define TPF_TERM_SOUND 0x0080
#define TPF_ABORTABLE 0x0100

// -------------------------------------------------------------------------
//  typedefs
// -------------------------------------------------------------------------
struct PresenterInfo
{
	uint16_t flags;
	uint16_t gflags;
	const char* script[TP_MAX_PAGES];
	void* scriptstart;
	int8_t numpages;
	int8_t pagenum;
	uint16_t xl;
	uint16_t yl;
	uint16_t xh;
	uint16_t yh;
	int8_t fontnumber;
	uint8_t bgcolor;
	uint8_t ltcolor;
	uint8_t dkcolor;
	uint8_t shcolor;
	uint16_t cur_x;
	uint16_t cur_y;
	int8_t print_delay;
	uint8_t highlight_color;
	uint8_t fontcolor;
	int16_t id_cache;
	char* infoline;
	int custom_line_height;
}; // PresenterInfo

enum pisType
{
	pis_pic,
	pis_sprite,
	pis_scaled,
	pis_scwall,
	pis_latchpic
}; // pisType

// -------------------------------------------------------------------------
// Function prototypes
// -------------------------------------------------------------------------
void TP_Presenter(
	PresenterInfo* pi);
void TP_WrapText();
void TP_HandleCodes();
int16_t TP_DrawShape(
	int16_t x,
	int16_t y,
	int16_t shapenum,
	pisType type);
uint16_t TP_VALUE(
	const char* ptr,
	int8_t num_nybbles);
int32_t TP_LoadScript(
	int lumpnum,
	PresenterInfo* pi);
void TP_FreeScript(
	PresenterInfo* pi);
void TP_InitScript(
	PresenterInfo* pi);
void TP_AnimatePage(
	int16_t numanims);
int16_t TP_BoxAroundShape(
	int16_t x1,
	int16_t y1,
	uint16_t shapenum,
	pisType shapetype);
void TP_JumpCursor();
void TP_Print(
	const char* str,
	bool single_char);
bool TP_SlowPrint(
	const char* str,
	int8_t delay);
void TP_PurgeAllGfx();
void TP_CachePage(
	const char* script);
int16_t TP_LineCommented(
	const char* s);
void TP_PrintPageNumber();

extern int TPscan; // ScanCode of the last key that ended the presenter.

#endif // __BLAKE_JM_TP_H__
