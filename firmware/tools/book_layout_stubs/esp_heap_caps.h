/*
 * SPDX-FileCopyrightText: 2026 mindreset
 * SPDX-License-Identifier: Apache-2.0
 * 中文：宿主布局测试的内存适配。/ English: Memory adapter for host layout tests.
 * 冻结：仅供测试。/ Frozen: Tests only.
 */
#pragma once
#include <stdlib.h>
#include <stdbool.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern int test_alloc_fail_after;
extern size_t test_allocation_peak;
static inline bool allocation_allowed(size_t n){if(n>test_allocation_peak)test_allocation_peak=n;if(test_alloc_fail_after==0)return false;if(test_alloc_fail_after>0)--test_alloc_fail_after;return true;}
static inline void* heap_caps_malloc(size_t n, int caps) { (void)caps; return allocation_allowed(n)?malloc(n):NULL; }
static inline void* heap_caps_realloc(void* p, size_t n, int caps) { (void)caps; return allocation_allowed(n)?realloc(p,n):NULL; }
