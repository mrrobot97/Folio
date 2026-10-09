/* SPDX-License-Identifier: Apache-2.0
 * 有界、校验完整的离线壁纸。/ Bounded, fully validated offline wallpapers.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FOLIO_WALLPAPER_WIDTH 684u
#define FOLIO_WALLPAPER_HEIGHT 1216u
#define FOLIO_WALLPAPER_BYTES (FOLIO_WALLPAPER_WIDTH * FOLIO_WALLPAPER_HEIGHT / 2)
#define FOLIO_WALLPAPER_HEADER 16u
/// FWP1，宽/高 u16，字节数/CRC32 u32，均小端；偶数像素在低半字节。
/// FWP1, u16 dimensions, u32 bytes/CRC32, little-endian; even pixels in the low nibble.
bool folio_wallpaper_decode(const uint8_t* file, size_t bytes, uint8_t* out, size_t capacity);
/// 只接受 /sdcard/wallpapers 下直接 .fwp 文件，不穿越目录。/ Direct .fwp under /sdcard/wallpapers only, no traversal.
bool folio_wallpaper_path_valid(const char* path);
/// 返回拥有的已校验图片，调用方 free；失败不改变 out。/ Return owned validated pixels; caller frees; out untouched on failure.
bool folio_wallpaper_read(const char* path, uint8_t** out);
