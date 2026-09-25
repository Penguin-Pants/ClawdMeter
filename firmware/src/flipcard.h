#pragma once
// Split-flap digit card — one red card showing a single digit, with a
// mechanical-style flip when the digit changes. Used by the "Limit reached"
// countdown in ui.cpp; board-agnostic (sizes come from the caller's Layout).
//
// The flip never uses LVGL transforms: a transformed object is rendered via a
// full ARGB8888 layer allocated from LVGL's built-in heap, which is tight on
// the no-PSRAM C6 and hangs on LV_ASSERT_MALLOC if it fails. Instead each flap
// is a plain object whose height shrinks/grows about the hinge while its digit
// slides with it — a clipped "squash" that costs no extra memory.

#include <lvgl.h>

struct FlipCardStyle {
    int16_t w, h;          // card size in px (h should be even)
    int16_t radius;        // outer corner radius
    const lv_font_t* font; // digit font
    lv_color_t hi;         // upper-half fill
    lv_color_t lo;         // lower-half fill
    lv_color_t text;       // digit color
};

struct FlipCard {
    lv_obj_t* root;
    lv_obj_t* top_half;   lv_obj_t* top_lbl;
    lv_obj_t* bot_half;   lv_obj_t* bot_lbl;
    lv_obj_t* top_flap;   lv_obj_t* top_flap_lbl;  lv_obj_t* top_flap_sq;
    lv_obj_t* bot_flap;   lv_obj_t* bot_flap_lbl;  lv_obj_t* bot_flap_sq;
    const FlipCardStyle* st;
    int16_t half_h;       // height of each half (card minus split, halved)
    int16_t lbl_y;        // label top in card coords that centers the glyph on the split
    char    cur;          // digit currently shown (0 = none yet)
    char    target;       // digit the running flip ends on
    bool    busy;         // a flip animation is running
};

// Build the card at (x, y) inside parent. `st` must outlive the card.
void flipcard_init(FlipCard* c, lv_obj_t* parent, int16_t x, int16_t y,
                   const FlipCardStyle* st);

// Show `digit` at once, cancelling any running flip.
void flipcard_set(FlipCard* c, char digit);

// Flip to `digit` (~300 ms). No-op if already showing it. A flip already in
// progress is finished instantly first.
void flipcard_flip_to(FlipCard* c, char digit);
