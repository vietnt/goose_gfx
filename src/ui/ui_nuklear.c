/* Nuklear's implementation, compiled once, with the configuration every
   file of the layer includes it with (ui_internal.h). Built without
   warnings: third-party code is not ours to act on. */

#define NK_IMPLEMENTATION
#include "ui_internal.h"

#include <limits.h>

/* Here, where Nuklear's own table is in scope. */
int64_t gs_ui_default_color_table(gs_ui_color_slice out) {
    for (int64_t i = 0; i < NK_COLOR_COUNT && i < out.len; i++) {
        struct nk_color c = nk_default_color_style[i];
        gs_ui_color g = { c.r, c.g, c.b, c.a };
        out.data[i] = g;
    }
    return NK_COLOR_COUNT;
}

/* Here, where nk_textedit_makeundo_insert is in scope. */
bool ui_textedit_paste(struct nk_text_edit *e, const char *text, int len) {
    if (e->mode == NK_TEXT_EDIT_MODE_VIEW || len <= 0) return false;
    nk_textedit_clamp(e);
    nk_textedit_delete_selection(e);
    const struct nk_buffer *b = &e->string.buffer;
    int room = b->type == NK_BUFFER_FIXED ? (int)b->memory.size - 1 - (int)b->allocated : INT_MAX;
    char *keep = (char *)malloc((size_t)len);
    if (!keep) return false;
    int n = 0, chars = 0, g;
    nk_rune r;
    for (int at = 0; at < len && (g = nk_utf_decode(text + at, &r, len - at)) > 0; at += g) {
        if (r == 127 || (r == '\n' && e->single_line) || (e->filter && !e->filter(e, r)))
            continue;
        if (g > room - n) break;
        memcpy(keep + n, text + at, (size_t)g);
        n += g;
        chars++;
    }
    bool pasted = n > 0 && nk_str_insert_at_rune(&e->string, e->cursor, keep, n);
    if (pasted) {
        nk_textedit_makeundo_insert(e, e->cursor, chars);
        e->cursor += chars;
        e->has_preferred_x = 0;
    }
    free(keep);
    return pasted;
}

/* Here, where Nuklear's property states are in scope. Typed text reaches
   a window only while it takes input: it is not hidden, closed or
   minimized, nor read only, which Nuklear makes the windows behind the
   one in front (background ones aside) and a window with a popup open.
   There the text goes to the property being typed into, or else to the
   text field that has the keyboard: edit.active, which a property sets
   too while it is dragged, if the window had a text field in that place
   last frame. */
static bool ui_takes_text(const struct nk_window *w) {
    if (w->flags & (NK_WINDOW_HIDDEN | NK_WINDOW_CLOSED | NK_WINDOW_MINIMIZED | NK_WINDOW_ROM))
        return false;
    if (w->property.active) return w->property.state == NK_PROPERTY_EDIT;
    return w->edit.active && w->edit.name < w->edit.old;
}

uint8_t gs_ui_wants_keyboard(gs_ui_context c) {
    UI_CTX(c, "ui::wants_keyboard", 0);
    for (const struct nk_window *w = ctx->begin; w; w = w->next) {
        if (!ui_window_current(u, w)) continue;
        if (ui_takes_text(w)) return 1;
        if (w->popup.active && w->popup.win && !(w->flags & NK_WINDOW_HIDDEN) &&
            ui_takes_text(w->popup.win))
            return 1;
    }
    return 0;
}

/* Every constant of ui_api.h has the value of Nuklear's of the same name,
   but the few that are the layer's own, which stand in for themselves (the
   end of the file, where Nuklear's NK_FILTER_FLOAT is no longer used). */
#define NK_FILTER_DEFAULT GS_UI_FILTER_DEFAULT
#define NK_FILTER_ASCII GS_UI_FILTER_ASCII
#define NK_FILTER_FLOAT GS_UI_FILTER_FLOAT
#define NK_FILTER_DECIMAL GS_UI_FILTER_DECIMAL
#define NK_FILTER_HEX GS_UI_FILTER_HEX
#define NK_FILTER_OCT GS_UI_FILTER_OCT
#define NK_FILTER_BINARY GS_UI_FILTER_BINARY
#define NK_RANGE_DEFAULT GS_UI_RANGE_DEFAULT
#define NK_RANGE_CHINESE GS_UI_RANGE_CHINESE
#define NK_RANGE_CYRILLIC GS_UI_RANGE_CYRILLIC
#define NK_RANGE_KOREAN GS_UI_RANGE_KOREAN
#define UI_SAME_AS_NUKLEAR(type, name, value) _Static_assert(GS_UI_##name == NK_##name, #name);
GS_UI_CONSTANTS(UI_SAME_AS_NUKLEAR)
#undef UI_SAME_AS_NUKLEAR

/* Here, where stb_truetype is in scope: text laid out as a browser lays it.
   Kerning as HarfBuzz applies it to a pair of glyphs: Nuklear lays text out
   by advances alone, and stb_truetype's GPOS reader stops at the first
   lookup that has the pair, where HarfBuzz adds every lookup the 'kern'
   feature lists; a font without GPOS kerning keeps its old 'kern' table,
   through stb. And each glyph on a whole pixel row: Nuklear puts a glyph's
   top half a pixel below its ascent without rounding, so the renderer's
   linear filter smeared every glyph over two rows, where a browser rounds
   the baseline. Both by wrapping the font's width and glyph query. */
struct ui_kern {
    stbtt_fontinfo info;
    float per_px;                       /* units to px, for a font 1 px high */
    bool kerned;                        /* it has kerning */
    int nlookups;
    const uint8_t *lookups[32];         /* the 'kern' lookups, in lookup order */
    nk_text_width_f width;              /* Nuklear's own */
    nk_query_font_glyph_f query;
};

static int ui_u16(const uint8_t *p) { return p[0] << 8 | p[1]; }
static int ui_s16(const uint8_t *p) { return (int16_t)(p[0] << 8 | p[1]); }
static int ui_value_size(int format) { return 2 * __builtin_popcount(format & 0xff); }

/* A pair adjustment subtable's x advance for the pair, and whether it
   applies: format 1 when it lists the pair, format 2 whenever it covers the
   first glyph, as HarfBuzz has it. */
static bool ui_pair_pos(const uint8_t *st, int g1, int g2, int *x) {
    int cov = stbtt__GetCoverageIndex((stbtt_uint8 *)st + ui_u16(st + 2), g1);
    if (cov < 0) return false;
    int vf1 = ui_u16(st + 4), vf2 = ui_u16(st + 6);
    int s1 = ui_value_size(vf1), s2 = ui_value_size(vf2);
    int at = 2 * __builtin_popcount(vf1 & 3);      /* past x and y placement */
    const uint8_t *rec = NULL;
    if (ui_u16(st) == 1) {
        if (cov >= ui_u16(st + 8)) return false;
        const uint8_t *set = st + ui_u16(st + 10 + 2 * cov);
        int lo = 0, hi = ui_u16(set) - 1, size = 2 + s1 + s2;
        while (lo <= hi && !rec) {
            int m = (lo + hi) >> 1;
            int g = ui_u16(set + 2 + size * m);
            if (g2 < g) hi = m - 1;
            else if (g2 > g) lo = m + 1;
            else rec = set + 2 + size * m + 2;
        }
        if (!rec) return false;
    } else if (ui_u16(st) == 2) {
        int c1 = stbtt__GetGlyphClass((stbtt_uint8 *)st + ui_u16(st + 8), g1);
        int c2 = stbtt__GetGlyphClass((stbtt_uint8 *)st + ui_u16(st + 10), g2);
        int n1 = ui_u16(st + 12), n2 = ui_u16(st + 14);
        if (c1 < 0 || c2 < 0 || c1 >= n1 || c2 >= n2) return false;
        rec = st + 16 + (c1 * n2 + c2) * (s1 + s2);
    } else {
        return false;
    }
    *x = (vf1 & 4) ? ui_s16(rec + at) : 0;
    return true;
}

static int ui_kern_units(const struct ui_kern *k, int g1, int g2) {
    if (!g1 || !g2 || !k->kerned) return 0;
    if (!k->nlookups) return stbtt_GetGlyphKernAdvance(&k->info, g1, g2);
    int sum = 0;
    for (int i = 0; i < k->nlookups; i++) {
        const uint8_t *l = k->lookups[i];
        int n = ui_u16(l + 4);
        for (int s = 0; s < n; s++) {
            const uint8_t *st = l + ui_u16(l + 6 + 2 * s);
            if (ui_u16(l) == 9) st += (uint32_t)ui_u16(st + 4) << 16 | ui_u16(st + 6);
            int x;
            if (ui_pair_pos(st, g1, g2, &x)) {
                sum += x;
                break;
            }
        }
    }
    return sum;
}

static struct ui_kern *ui_kern_of(const struct nk_font *font) {
    for (uint32_t i = 0; i < ui_fonts.count; i++) {
        ui_font *f = (ui_font *)ui_table_at(&ui_fonts, i);
        if (f && f->font == font) return f->kern;
    }
    return NULL;
}

static float ui_kern_px(const struct ui_kern *k, float height, nk_rune a, nk_rune b) {
    int u = ui_kern_units(k, stbtt_FindGlyphIndex(&k->info, (int)a), stbtt_FindGlyphIndex(&k->info, (int)b));
    return (float)u * k->per_px * height;
}

static float ui_kerned_width(nk_handle h, float height, const char *text, int len) {
    const struct ui_kern *k = ui_kern_of((const struct nk_font *)h.ptr);
    float w = k->width(h, height, text, len);
    if (!k->kerned) return w;
    nk_rune prev = 0, r;
    int g;
    for (int at = 0; at < len && (g = nk_utf_decode(text + at, &r, len - at)) > 0; at += g) {
        if (r == NK_UTF_INVALID) break;
        if (prev) w += ui_kern_px(k, height, prev, r);
        prev = r;
    }
    return w;
}

static void ui_kerned_query(nk_handle h, float height, struct nk_user_font_glyph *glyph,
                            nk_rune codepoint, nk_rune next) {
    const struct ui_kern *k = ui_kern_of((const struct nk_font *)h.ptr);
    const struct nk_font *font = (const struct nk_font *)h.ptr;
    k->query(h, height, glyph, codepoint, next);
    if (next) glyph->xadvance += ui_kern_px(k, height, codepoint, next);
    /* the top at the baked size, floored: Nuklear's half pixel rounds it */
    float scale = height / font->info.height;
    glyph->offset.y = floorf(glyph->offset.y / scale + 1e-3f) * scale;
}

/* Lays the font out so; again after each bake, which sets Nuklear's own
   functions back. */
void ui_font_kerning(ui_font *f) {
    struct nk_font *font = f->font;
    if (!f->kern) {
        const unsigned char *blob = (const unsigned char *)font->config->ttf_blob;
        struct ui_kern *k = (struct ui_kern *)calloc(1, sizeof *k);
        if (!k || !stbtt_InitFont(&k->info, blob, stbtt_GetFontOffsetForIndex(blob, 0))) {
            free(k);
            return;
        }
        const uint8_t *g = k->info.data + k->info.gpos;
        if (k->info.gpos && ui_u16(g) == 1) {
            const uint8_t *features = g + ui_u16(g + 6), *lookups = g + ui_u16(g + 8);
            bool want[1024] = { false };
            for (int i = 0; i < ui_u16(features); i++) {
                const uint8_t *rec = features + 2 + 6 * i;
                if (memcmp(rec, "kern", 4)) continue;
                const uint8_t *ft = features + ui_u16(rec + 4);
                for (int j = 0; j < ui_u16(ft + 2); j++) {
                    int li = ui_u16(ft + 4 + 2 * j);
                    if (li < 1024) want[li] = true;
                }
            }
            for (int li = 0; li < ui_u16(lookups) && li < 1024 && k->nlookups < 32; li++) {
                if (!want[li]) continue;
                const uint8_t *l = lookups + ui_u16(lookups + 2 + 2 * li);
                int type = ui_u16(l);
                if (type == 9 && ui_u16(l + 4)) type = ui_u16(l + ui_u16(l + 6) + 2);
                if (type == 2) k->lookups[k->nlookups++] = l;
            }
        }
        k->kerned = k->nlookups || k->info.kern;
        k->per_px = stbtt_ScaleForPixelHeight(&k->info, 1.0f);
        f->kern = k;
    }
    f->kern->width = font->handle.width;
    f->kern->query = font->handle.query;
    font->handle.width = ui_kerned_width;
    font->handle.query = ui_kerned_query;
}
