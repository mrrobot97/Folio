/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 *
 * 中文：借鉴阅读演示的逐码点折行，建立 PSRAM 页表并绘制章节。
 * English: Adapt the reading demo's codepoint wrapping into PSRAM chapter pagination.
 *
 * 冻结：原文由调用方持有；字体测量和绘制必须由调用方串行化。
 * 为尽早进入阅读，支持分批分页；未加载图片绘制可点击占位及已读重复提示，不在绘图时读取资源或解码。
 * Frozen: Caller owns source text and serializes all font measurement and drawing.
 * Incremental pagination enables early reading; unloaded images show clickable placeholders and visited-chapter repeat hints; never read or decode resources while drawing.
 */
#include "book_layout.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "ttf_font.h"

#define PAGE_MAX 4096u
#define LINE_BYTES_MAX 4096u
#define PSRAM_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)

struct book_layout_candidate {
    const char* text;
    size_t len;
    size_t* pages;
    size_t count;
    size_t capacity;
    char* line;
    size_t line_capacity;
    EpdRect rect;
    int px;
    size_t scan;
    int64_t used;
    bool complete;
    const blk_t* blocks;
    size_t block_count;
    bool formatted;
    folio_format_values_t values;
};
static struct book_layout_candidate s_primary;
static struct book_layout_candidate* s_layout = &s_primary;
static book_layout_error_t s_last_error;
static uint32_t s_missing_codepoint;
book_layout_error_t book_layout_last_error(uint32_t* missing){if(missing)*missing=s_missing_codepoint;return s_last_error;}
#define s_text (s_layout->text)
#define s_len (s_layout->len)
#define s_pages (s_layout->pages)
#define s_count (s_layout->count)
#define s_capacity (s_layout->capacity)
#define s_line (s_layout->line)
#define s_line_capacity (s_layout->line_capacity)
#define s_rect (s_layout->rect)
#define s_px (s_layout->px)
#define s_scan (s_layout->scan)
#define s_used (s_layout->used)
#define s_complete (s_layout->complete)
#define s_blocks (s_layout->blocks)
#define s_block_count (s_layout->block_count)
#define s_formatted (s_layout->formatted)
#define s_values (s_layout->values)

// 块表是有序字节区间，二分查找当前行样式。/ Blocks are ordered byte ranges; binary-search the line style.
static const blk_t* block_at(size_t off) {
    if (!s_block_count) return NULL;
    size_t lo = 0, hi = s_block_count;
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_blocks[mid].offset <= off) lo = mid;
        else hi = mid;
    }
    return &s_blocks[lo];
}
// 插图等比适配正文区域，不放大；整块换页，不切断图片。
// Fit illustrations proportionally without upscaling; move whole images across page boundaries.
static void image_size(const blk_t* block, int* width, int* height) {
    *width = block->image_width; *height = block->image_height;
    if (*width > s_rect.width) { *height = (int)((int64_t)*height * s_rect.width / *width); *width = s_rect.width; }
    if (*height > s_rect.height) { *width = (int)((int64_t)*width * s_rect.height / *height); *height = s_rect.height; }
    if (*width < 1) *width = 1;
    if (*height < 1) *height = 1;
}
static int row_height(const blk_t* block, int px) {
    if (block && block->image) { int width, height; image_size(block, &width, &height); return height; }
    if (block && block->image_src) return 2 * s_px < s_rect.height ? 2 * s_px : s_rect.height;
    return s_formatted ? folio_format_line_height(&s_values, s_px + (block && block->heading ? 8 : 0)) : px + px / 2;
}
static EpdRect placeholder_rect(EpdRect body, int top, int height) {
    int pad = body.width > 24 && height > 16 ? 8 : 0;
    return (EpdRect){body.x + pad, body.y + top + pad / 2, body.width - 2 * pad, height - pad};
}
static void draw_placeholder(uint8_t* fb, const blk_t* block, EpdRect rect) {
    uint8_t ink=s_formatted?folio_format_ink(&s_values):0,paper=s_formatted?folio_format_paper(&s_values):15;
    for (int x = rect.x; x < rect.x + rect.width; ++x) {
        epd_draw_pixel(x, rect.y, ink, fb); epd_draw_pixel(x, rect.y + rect.height - 1, ink, fb);
    }
    for (int y = rect.y; y < rect.y + rect.height; ++y) {
        epd_draw_pixel(rect.x, y, ink, fb); epd_draw_pixel(rect.x + rect.width - 1, y, ink, fb);
    }
    int px = s_px < 32 ? s_px : 32;
    bool repeat = block->image_repeated && !block->image_title;
    const char* label = block->image_title ? "标题图 · 点击查看" : repeat ? "重复图片 · 点击查看" : "点击查看图片";
    if (ttf_text_width_px(px, label) > rect.width - 16) return;
    int lines = repeat ? 2 : 1;
    int top = rect.y + (rect.height - lines * (px + 4)) / 2;
    ttf_draw_text_px(fb, rect.x + rect.width / 2, top + ttf_ascender_px(px), px,
                     label, EPD_DRAW_ALIGN_CENTER, ink, paper);
    if (repeat) {
        char origin[64]; snprintf(origin, sizeof(origin), "已读最早：第 %u 节", (unsigned)block->image_first_chapter + 1);
        int small = px < 24 ? px : 24;
        if (ttf_text_width_px(small, origin) <= rect.width - 16)
            ttf_draw_text_px(fb, rect.x + rect.width / 2, top + px + 4 + ttf_ascender_px(small), small,
                             origin, EPD_DRAW_ALIGN_CENTER, ink, paper);
    }
}
// 拒绝截断、过长编码、代理项和嵌入零字节。/ Reject truncation, overlong encodings, surrogates and embedded NUL.
static size_t codepoint_size(const char* text, size_t remaining) {
    if (!remaining) return 0;
    const unsigned char* p = (const unsigned char*)text;
    if (p[0] > 0 && p[0] < 0x80) return 1;
    size_t n = p[0] >= 0xc2 && p[0] <= 0xdf ? 2 :
               p[0] >= 0xe0 && p[0] <= 0xef ? 3 :
               p[0] >= 0xf0 && p[0] <= 0xf4 ? 4 : 0;
    if (!n || n > remaining) return 0;
    for (size_t i = 1; i < n; i++) if ((p[i] & 0xc0) != 0x80) return 0;
    if ((p[0] == 0xe0 && p[1] < 0xa0) || (p[0] == 0xed && p[1] >= 0xa0) ||
        (p[0] == 0xf0 && p[1] < 0x90) || (p[0] == 0xf4 && p[1] >= 0x90)) return 0;
    return n;
}

void book_layout_free(void) {
    free(s_pages);
    free(s_line);
    s_pages = NULL;
    s_line = NULL;
    s_line_capacity = 0;
    s_text = NULL;
    s_count = s_capacity = s_len = 0;
    s_px = 0;
    s_scan = 0;
    s_used = 0;
    s_complete = false;
    s_formatted = false;
    s_blocks = NULL;
    s_block_count = 0;
}

static bool append_page(size_t off) {
    if (s_count == PAGE_MAX) return false;
    if (s_count == s_capacity) {
        size_t cap = s_capacity ? s_capacity * 2 : 16;
        size_t* pages = heap_caps_realloc(s_pages, cap * sizeof(*pages), PSRAM_CAPS);
        if (!pages) {s_last_error=BOOK_LAYOUT_ERROR_MEMORY;return false;}
        s_pages = pages;
        s_capacity = cap;
    }
    s_pages[s_count++] = off;
    return true;
}

static int line_indent(size_t off, bool heading) {
    if (!s_formatted || heading || !s_values.indent_em) return 0;
    bool first = !off || s_text[off - 1] == '\n' || s_text[off - 1] == '\r';
    int em=s_values.legacy_native_px?ttf_text_width_px(s_values.legacy_native_px,"　"):s_px;
    return first ? s_values.indent_em * em : 0;
}
static int paragraph_gap(int height, bool heading) { return height / (heading ? 2 : s_formatted ? 4 : 3); }
static uint32_t scalar_at(size_t off) {
    const unsigned char* p=(const unsigned char*)s_text+off;
    size_t n=codepoint_size(s_text+off,s_len-off);if(!n)return 0;
    uint32_t cp=n==1?p[0]:p[0]&((1U<<(7-n))-1U);
    for(size_t i=1;i<n;i++)cp=(cp<<6)|(p[i]&63U);
    return cp;
}
static bool closing_punctuation(uint32_t cp) {
    const uint32_t chars[]={',','.','!','?',':',';',')',']','}',0x3001,0x3002,0xff0c,0xff01,0xff1f,0xff1a,0xff1b,0xff09,0x3009,0x300b,0x300d,0x300f,0x3011,0x201d,0x2019,0x2026};
    for(size_t i=0;i<sizeof(chars)/sizeof(*chars);i++)if(cp==chars[i])return true;
    return false;
}
static bool opening_punctuation(uint32_t cp) {
    const uint32_t chars[]={'(','[','{',0xff08,0x3008,0x300a,0x300c,0x300e,0x3010,0x201c,0x2018};
    for(size_t i=0;i<sizeof(chars)/sizeof(*chars);i++)if(cp==chars[i])return true;
    return false;
}
static size_t previous_scalar(size_t off) {
    if(!off)return 0;
    --off;while(off&&((unsigned char)s_text[off]&0xc0)==0x80)--off;return off;
}
static bool reserve_line(size_t required) {
    if(required>LINE_BYTES_MAX)return false;
    if(required<=s_line_capacity)return true;
    size_t cap=s_line_capacity?s_line_capacity:256;
    while(cap<required)cap*=2;
    char* line=heap_caps_realloc(s_line,cap,PSRAM_CAPS);
    if(!line){s_last_error=BOOK_LAYOUT_ERROR_MEMORY;return false;}
    s_line=line;s_line_capacity=cap;return true;
}
// 折行时保留原文字节位置；CRLF 算一个段落边界。/ Preserve source offsets while wrapping; CRLF is one paragraph boundary.
static bool take_line(size_t off, size_t* next, bool* paragraph_end, int* px, bool* heading) {
    const blk_t* block = block_at(off);
    *heading = block && block->heading;
    *px = s_px + (*heading ? 8 : 0);
    if (s_formatted) { *px = s_values.legacy_native_px?s_values.legacy_native_px+(*heading?8:0):ttf_font_pixel_height_for_em(*px); if (!*px) return false; }
    size_t limit = block ? block->offset + block->len : s_len;
    size_t end = off;
    int64_t width = line_indent(off, *heading);
    s_line[0] = 0;
    *paragraph_end = false;
    if (block && (block->image || block->image_src)) {
        *heading = false;
        *next = limit + (limit < s_len && s_text[limit] == '\n');
        return *next > off;
    }
    while (end < limit && s_text[end] != '\r' && s_text[end] != '\n') {
        size_t n = codepoint_size(s_text + end, s_len - end);
        if (!n) return false;
        if(s_formatted){
            uint32_t cp=scalar_at(end);
            if(cp>=32&&!ttf_font_has_codepoint(cp)){
                s_missing_codepoint=cp;s_last_error=ttf_font_resource_failed()?BOOK_LAYOUT_ERROR_FONT_RESOURCE:BOOK_LAYOUT_ERROR_GLYPH;return false;
            }
        }
        // 字体逐字取整后累加 advance；单字测量避免反复扫描整行前缀。
        // Font advances are rounded per glyph and summed; measure each glyph once instead of every prefix.
        char glyph[5];
        memcpy(glyph, s_text + end, n);
        if(n==1&&glyph[0]=='\t')glyph[0]=' ';
        glyph[n] = 0;
        int64_t candidate = width + ttf_text_width_px(*px, glyph);
        if (candidate < 0) return false;
        if (candidate > s_rect.width) {
            if (end == off) return false;
            size_t rollback=previous_scalar(end);
            if(closing_punctuation(scalar_at(end))){
                while(rollback>off&&closing_punctuation(scalar_at(rollback)))rollback=previous_scalar(rollback);
                if(rollback>off){end=rollback;s_line[end-off]=0;}
            }else if(opening_punctuation(scalar_at(rollback))){
                while(rollback>off&&opening_punctuation(scalar_at(previous_scalar(rollback))))rollback=previous_scalar(rollback);
                if(rollback>off){end=rollback;s_line[end-off]=0;}
            }
            break;
        }
        // 行缓冲按实际 UTF-8 字节增长；恶意零宽字符不能耗尽整章大小的 RAM。
        // Grow by actual UTF-8 row bytes; malicious zero-width runs cannot consume chapter-sized RAM.
        if(!reserve_line(end-off+n+1))return false;
        width = candidate;
        memcpy(s_line + end - off, glyph, n);
        s_line[end - off + n] = 0;
        end += n;
    }
    *next = end;
    if (end < s_len && (s_text[end] == '\r' || s_text[end] == '\n')) {
        *next = end + 1;
        if (s_text[end] == '\r' && *next < s_len && s_text[*next] == '\n') (*next)++;
        *paragraph_end = true;
    }
    return *next > off;
}

bool book_layout_build(const char* utf8, size_t len, EpdRect rect, int px) {
    return book_layout_build_blocks(utf8, len, NULL, 0, rect, px);
}

bool book_layout_build_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    return book_layout_begin_blocks(utf8, len, blocks, count, rect, px) && book_layout_extend(PAGE_MAX);
}

static bool begin_layout(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px, const folio_format_values_t* values) {
    s_last_error=BOOK_LAYOUT_ERROR_NONE;s_missing_codepoint=0;
    book_layout_free();
    if (values) { if (!folio_format_values_valid(values)) return false; s_formatted = true; s_values = *values; }
    if ((!utf8 && len) || len == SIZE_MAX || px <= 0 || px > INT_MAX / 3 ||
        rect.width <= 0 || rect.height <= 0 || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return false;
    int line_height = values ? folio_format_line_height(values, px) : px + px / 2;
    if (line_height > rect.height) return false;
    for (size_t off = 0; off < len;) {
        size_t n = codepoint_size(utf8 + off, len - off);
        if (!n) return false;
        off += n;
    }
    if (count) {
        if (!blocks || count > HTML_TEXT_MAX_BLOCKS) return false;
        size_t expected = 0;
        for (size_t i = 0; i < count; ++i) {
            const blk_t* b = &blocks[i];
            if (b->image && (!b->image_width || !b->image_height)) return false;
            if (b->offset != expected || b->offset >= len || !b->len || b->len > len - b->offset ||
                ((unsigned char)utf8[b->offset] & 0xc0) == 0x80) return false;
            size_t end = b->offset + b->len;
            if (i + 1 < count) {
                if (end >= len || utf8[end] != '\n') return false;
                expected = end + 1;
            } else if (end != len) return false;
        }
    }
    s_blocks = blocks;
    s_block_count = count;
    s_text = utf8;
    s_len = len;
    s_px = px;
    s_rect = rect;
    if (!reserve_line(1) || !append_page(0)) goto fail;
    return book_layout_extend(2);
fail:
    if(s_last_error==BOOK_LAYOUT_ERROR_NONE)s_last_error=BOOK_LAYOUT_ERROR_INVALID;
    book_layout_free();
    return false;
}

bool book_layout_begin_blocks(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, int px) {
    return begin_layout(utf8, len, blocks, count, rect, px, NULL);
}
bool book_layout_begin_formatted(const char* utf8, size_t len, const blk_t* blocks, size_t count, EpdRect rect, const folio_format_values_t* values) {
    return values && begin_layout(utf8, len, blocks, count, rect, values->px, values);
}
book_layout_candidate_t* book_layout_prepare_format(const char* utf8,size_t len,const blk_t* blocks,size_t count,
        EpdRect rect,const folio_format_values_t* values,size_t anchor) {
    book_layout_candidate_t* candidate = heap_caps_malloc(sizeof(*candidate), PSRAM_CAPS);
    if (!candidate) {s_last_error=BOOK_LAYOUT_ERROR_MEMORY;return NULL;}
    memset(candidate, 0, sizeof(*candidate));
    book_layout_candidate_t* previous = s_layout;
    s_layout = candidate;
    bool ok = book_layout_begin_formatted(utf8,len,blocks,count,rect,values);
    while (ok && !book_layout_complete() && book_layout_page_start_offset(book_layout_page_count()) <= anchor)
        ok = book_layout_extend(2);
    s_layout = previous;
    if (!ok) { book_layout_discard_format(candidate); return NULL; }
    return candidate;
}
void book_layout_discard_format(book_layout_candidate_t* candidate) {
    if (!candidate || candidate == &s_primary) return;
    book_layout_candidate_t* previous = s_layout;
    s_layout = candidate; book_layout_free(); s_layout = previous;
    free(candidate);
}
bool book_layout_commit_format(book_layout_candidate_t* candidate) {
    if (!candidate || candidate == &s_primary || s_layout != &s_primary || !candidate->count) return false;
    book_layout_candidate_t old = s_primary;
    s_primary = *candidate; *candidate = old;
    book_layout_discard_format(candidate);
    return true;
}
size_t book_layout_memory_bytes(void){return sizeof(s_primary)+s_primary.line_capacity+s_primary.capacity*sizeof(size_t);}
size_t book_layout_candidate_memory_bytes(const book_layout_candidate_t* candidate){return candidate?sizeof(*candidate)+candidate->line_capacity+candidate->capacity*sizeof(size_t):0;}

bool book_layout_extend(size_t pages) {
    if (!s_count) return false;
    if (!pages || s_complete) return true;
    size_t target = pages > PAGE_MAX - book_layout_page_count() ? PAGE_MAX : book_layout_page_count() + pages;
    while (s_scan < s_len) {
        size_t next;
        bool paragraph_end, heading;
        int line_px;
        if (!take_line(s_scan, &next, &paragraph_end, &line_px, &heading)) goto fail;
        const blk_t* block = block_at(s_scan);
        int line_height = row_height(block, line_px);
        if (line_height > s_rect.height) goto fail;
        if (s_used + line_height > s_rect.height) {
            if (!append_page(s_scan)) goto fail;
            s_used = 0;
            if (book_layout_page_count() >= target) return true;
        }
        s_used += line_height;
        if (paragraph_end) s_used += paragraph_gap(line_height, heading);
        s_scan = next;
    }
    s_complete = true;
    return true;
fail:
    if(s_last_error==BOOK_LAYOUT_ERROR_GLYPH||s_last_error==BOOK_LAYOUT_ERROR_FONT_RESOURCE)return false;
    if(s_last_error==BOOK_LAYOUT_ERROR_NONE)s_last_error=BOOK_LAYOUT_ERROR_INVALID;
    book_layout_free();
    return false;
}

size_t book_layout_page_count(void) { return s_count ? s_count - !s_complete : 0; }
bool book_layout_complete(void) { return s_complete; }

size_t book_layout_page_start_offset(size_t page) {
    return page < s_count ? s_pages[page] : s_len;
}

size_t book_layout_page_for_offset(size_t off) {
    size_t lo = 0, hi = book_layout_page_count();
    while (lo + 1 < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (s_pages[mid] <= off) lo = mid;
        else hi = mid;
    }
    return lo;
}

void book_layout_draw_page(uint8_t* fb, size_t page, EpdRect rect, int px) {
    if (!fb || page >= book_layout_page_count() || px != s_px || rect.width != s_rect.width ||
        rect.height != s_rect.height || rect.x < 0 || rect.y < 0 ||
        rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height) return;
    size_t off = s_pages[page];
    size_t end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = 0;
    while (off < end) {
        size_t next;
        bool paragraph_end, heading;
        int line_px;
        if (!take_line(off, &next, &paragraph_end, &line_px, &heading)) return;
        const blk_t* block = block_at(off);
        int line_height = row_height(block, line_px);
        int image_width = 0;
        if (block && block->image) image_size(block, &image_width, &line_height);
        if (used + line_height > rect.height) return;
        if (image_width) {
            int left = rect.x + (rect.width - image_width) / 2;
            for (int y = 0; y < line_height; ++y) for (int x = 0; x < image_width; ++x) {
                size_t src = (size_t)((int64_t)y * block->image_height / line_height) * block->image_width +
                             (size_t)((int64_t)x * block->image_width / image_width);
                epd_draw_pixel(left + x, rect.y + (int)used + y, block->image[src], fb);
            }
        } else if (block && block->image_src) {
            draw_placeholder(fb, block, placeholder_rect(rect, (int)used, line_height));
        } else if (s_line[0]) {
            ttf_draw_text_px(fb, rect.x + line_indent(off, heading), rect.y + (int)used + ttf_ascender_px(line_px), line_px,
                             s_line, EPD_DRAW_ALIGN_LEFT, s_formatted ? folio_format_ink(&s_values) : 0,
                             s_formatted ? folio_format_paper(&s_values) : 15);
        }
        used += line_height;
        if (paragraph_end) used += paragraph_gap(line_height, heading);
        off = next;
    }
}
size_t book_layout_image_at(size_t page, EpdRect rect, int x, int y, EpdRect* hit) {
    if (page >= book_layout_page_count() || rect.width != s_rect.width || rect.height != s_rect.height ||
        rect.x < 0 || rect.y < 0 || rect.x > INT_MAX - rect.width || rect.y > INT_MAX - rect.height ||
        x < rect.x || y < rect.y || x >= rect.x + rect.width || y >= rect.y + rect.height) return SIZE_MAX;
    size_t off = s_pages[page], end = page + 1 < s_count ? s_pages[page + 1] : s_len;
    int64_t used = 0;
    while (off < end) {
        size_t next; bool paragraph_end, heading; int px;
        if (!take_line(off, &next, &paragraph_end, &px, &heading)) return SIZE_MAX;
        const blk_t* block = block_at(off);
        int height = row_height(block, px);
        if (used + height > rect.height) return SIZE_MAX;
        if (block && block->image_src && !block->image) {
            EpdRect area = placeholder_rect(rect, (int)used, height);
            if (x >= area.x && x < area.x + area.width && y >= area.y && y < area.y + area.height) {
                if (hit) *hit = area;
                return (size_t)(block - s_blocks);
            }
        }
        used += height;
        if (paragraph_end) used += paragraph_gap(height, heading);
        off = next;
    }
    return SIZE_MAX;
}
