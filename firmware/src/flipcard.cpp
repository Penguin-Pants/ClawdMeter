#include "flipcard.h"

#define FLIP_SPLIT_PX   2      // black gap between the two halves
#define FLIP_PHASE_MS   150    // fold time, then the same again for the drop
#define FLIP_SCALE_MAX  1024   // anim value for a fully open flap
#define FLIP_SHADE_MIN  140    // flap brightness (of 255) when edge-on to the viewer

// Plain, non-interactive rectangle — taps fall through to the screen behind so
// the usual tap-to-splash gesture still works on top of the cards.
static lv_obj_t* make_rect(lv_obj_t* parent, lv_color_t color) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t* make_digit(lv_obj_t* parent, const FlipCardStyle* st) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_width(l, st->w);
    lv_obj_set_style_text_font(l, st->font, 0);
    lv_obj_set_style_text_color(l, st->text, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

// One half of the card: a rounded rect whose inner edge (the one at the split)
// is squared off by a same-colored strip. Returns the strip via out_sq so the
// flaps can resize it as they fold.
static lv_obj_t* make_half(lv_obj_t* parent, const FlipCardStyle* st, lv_color_t color,
                           lv_obj_t** out_sq, lv_obj_t** out_lbl) {
    lv_obj_t* half = make_rect(parent, color);
    lv_obj_set_style_radius(half, st->radius, 0);
    *out_sq = make_rect(half, color);     // before the label so it never covers the digit
    *out_lbl = make_digit(half, st);
    return half;
}

static void set_digit(lv_obj_t* lbl, char d) {
    char s[2] = { d, 0 };
    lv_label_set_text(lbl, s);
}

static lv_color_t shade(lv_color_t c, int32_t v) {
    uint8_t mix = FLIP_SHADE_MIN + (255 - FLIP_SHADE_MIN) * v / FLIP_SCALE_MAX;
    return lv_color_mix(c, lv_color_black(), mix);
}

// Upper flap folding down toward the hinge. v = FLIP_SCALE_MAX (flat, fully
// covering the upper half) → 0 (edge-on). The flap is bottom-anchored at the
// split; its digit slides toward the hinge by half the lost height so the
// visible slice reads as a squash rather than a wipe.
static void apply_top(FlipCard* c, int32_t v) {
    const FlipCardStyle* st = c->st;
    int16_t h = c->half_h * v / FLIP_SCALE_MAX;
    int16_t y = c->half_h - h;
    int16_t t = (c->half_h / 2) * (FLIP_SCALE_MAX - v) / FLIP_SCALE_MAX;
    int16_t r = LV_MIN(st->radius, h / 2);
    int16_t sq = LV_MIN(st->radius, h);
    lv_color_t col = shade(st->hi, v);

    lv_obj_set_pos(c->top_flap, 0, y);
    lv_obj_set_size(c->top_flap, st->w, h);
    lv_obj_set_style_radius(c->top_flap, r, 0);
    lv_obj_set_style_bg_color(c->top_flap, col, 0);
    lv_obj_set_pos(c->top_flap_sq, 0, h - sq);
    lv_obj_set_size(c->top_flap_sq, st->w, sq);
    lv_obj_set_style_bg_color(c->top_flap_sq, col, 0);
    lv_obj_set_pos(c->top_flap_lbl, 0, c->lbl_y + t - y);
}

// Lower flap dropping from the hinge. v = 0 (edge-on) → FLIP_SCALE_MAX (flat,
// fully covering the lower half). Top-anchored just below the split.
static void apply_bot(FlipCard* c, int32_t v) {
    const FlipCardStyle* st = c->st;
    int16_t y = c->half_h + FLIP_SPLIT_PX;
    int16_t h = c->half_h * v / FLIP_SCALE_MAX;
    int16_t t = (c->half_h / 2) * (FLIP_SCALE_MAX - v) / FLIP_SCALE_MAX;
    int16_t r = LV_MIN(st->radius, h / 2);
    int16_t sq = LV_MIN(st->radius, h);
    lv_color_t col = shade(st->lo, v);

    lv_obj_set_pos(c->bot_flap, 0, y);
    lv_obj_set_size(c->bot_flap, st->w, h);
    lv_obj_set_style_radius(c->bot_flap, r, 0);
    lv_obj_set_style_bg_color(c->bot_flap, col, 0);
    lv_obj_set_pos(c->bot_flap_sq, 0, 0);
    lv_obj_set_size(c->bot_flap_sq, st->w, sq);
    lv_obj_set_style_bg_color(c->bot_flap_sq, col, 0);
    lv_obj_set_pos(c->bot_flap_lbl, 0, c->lbl_y - y - t);
}

static void top_exec_cb(void* var, int32_t v) { apply_top((FlipCard*)var, v); }
static void bot_exec_cb(void* var, int32_t v) { apply_bot((FlipCard*)var, v); }

static void drop_done_cb(lv_anim_t* a) {
    FlipCard* c = (FlipCard*)a->var;
    set_digit(c->bot_lbl, c->target);
    lv_obj_add_flag(c->bot_flap, LV_OBJ_FLAG_HIDDEN);
    c->cur = c->target;
    c->busy = false;
}

static void fold_done_cb(lv_anim_t* a) {
    FlipCard* c = (FlipCard*)a->var;
    lv_obj_add_flag(c->top_flap, LV_OBJ_FLAG_HIDDEN);
    apply_bot(c, 0);
    lv_obj_clear_flag(c->bot_flap, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t b;
    lv_anim_init(&b);
    lv_anim_set_var(&b, c);
    lv_anim_set_exec_cb(&b, bot_exec_cb);
    lv_anim_set_values(&b, 0, FLIP_SCALE_MAX);
    lv_anim_set_duration(&b, FLIP_PHASE_MS);
    lv_anim_set_path_cb(&b, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&b, drop_done_cb);
    lv_anim_start(&b);
}

void flipcard_init(FlipCard* c, lv_obj_t* parent, int16_t x, int16_t y,
                   const FlipCardStyle* st) {
    *c = {};
    c->st = st;
    c->half_h = (st->h - FLIP_SPLIT_PX) / 2;
    c->lbl_y = st->h / 2 - lv_font_get_line_height(st->font) / 2;
    int16_t bot_y = c->half_h + FLIP_SPLIT_PX;

    c->root = lv_obj_create(parent);
    lv_obj_remove_style_all(c->root);
    lv_obj_clear_flag(c->root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(c->root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(c->root, x, y);
    lv_obj_set_size(c->root, st->w, st->h);

    lv_obj_t* sq;
    c->top_half = make_half(c->root, st, st->hi, &sq, &c->top_lbl);
    lv_obj_set_pos(c->top_half, 0, 0);
    lv_obj_set_size(c->top_half, st->w, c->half_h);
    lv_obj_set_pos(sq, 0, c->half_h - st->radius);
    lv_obj_set_size(sq, st->w, st->radius);
    lv_obj_set_pos(c->top_lbl, 0, c->lbl_y);

    c->bot_half = make_half(c->root, st, st->lo, &sq, &c->bot_lbl);
    lv_obj_set_pos(c->bot_half, 0, bot_y);
    lv_obj_set_size(c->bot_half, st->w, c->half_h);
    lv_obj_set_pos(sq, 0, 0);
    lv_obj_set_size(sq, st->w, st->radius);
    lv_obj_set_pos(c->bot_lbl, 0, c->lbl_y - bot_y);

    // Flaps are created last so they draw over both halves.
    c->top_flap = make_half(c->root, st, st->hi, &c->top_flap_sq, &c->top_flap_lbl);
    c->bot_flap = make_half(c->root, st, st->lo, &c->bot_flap_sq, &c->bot_flap_lbl);
    lv_obj_add_flag(c->top_flap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->bot_flap, LV_OBJ_FLAG_HIDDEN);
}

void flipcard_set(FlipCard* c, char digit) {
    lv_anim_delete(c, NULL);
    lv_obj_add_flag(c->top_flap, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(c->bot_flap, LV_OBJ_FLAG_HIDDEN);
    set_digit(c->top_lbl, digit);
    set_digit(c->bot_lbl, digit);
    c->cur = c->target = digit;
    c->busy = false;
}

void flipcard_flip_to(FlipCard* c, char digit) {
    if (c->busy) flipcard_set(c, c->target);   // land the running flip at once
    if (digit == c->cur) return;
    if (c->cur == 0) { flipcard_set(c, digit); return; }

    // New digit waits behind the upper flap; the old lower half stays until
    // the lower flap lands on it.
    set_digit(c->top_lbl, digit);
    set_digit(c->bot_lbl, c->cur);
    set_digit(c->top_flap_lbl, c->cur);
    set_digit(c->bot_flap_lbl, digit);
    c->target = digit;
    c->busy = true;

    apply_top(c, FLIP_SCALE_MAX);
    lv_obj_clear_flag(c->top_flap, LV_OBJ_FLAG_HIDDEN);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, c);
    lv_anim_set_exec_cb(&a, top_exec_cb);
    lv_anim_set_values(&a, FLIP_SCALE_MAX, 0);
    lv_anim_set_duration(&a, FLIP_PHASE_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_completed_cb(&a, fold_done_cb);
    lv_anim_start(&a);
}
