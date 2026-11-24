#ifndef MB17_RECURSIVE_TRACE_H
#define MB17_RECURSIVE_TRACE_H
#include <stddef.h>
#include <stdint.h>
size_t mb17_recursive_enter(const uint32_t *, const uint32_t *, size_t);
void mb17_recursive_scratch(size_t, const char *, size_t);
void mb17_recursive_difference(size_t, const uint32_t *, const uint32_t *, size_t);
void mb17_recursive_leaf(size_t, size_t);
void mb17_recursive_leave(size_t, const uint32_t *, size_t, int);
#endif
