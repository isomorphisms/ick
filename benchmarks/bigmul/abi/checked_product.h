#ifndef MB02_CHECKED_PRODUCT_H
#define MB02_CHECKED_PRODUCT_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MB02_CHECKED_PRODUCT_ABI_VERSION 1

typedef int (*mb02_multiply_fn)(const uint32_t *, size_t,
                                const uint32_t *, size_t, uint32_t *);

enum mb02_checked_status {
    MB02_CHECKED_OK = 0,
    MB02_CHECKED_ADMISSION = 1,
    MB02_CHECKED_ALLOCATION = 2,
    MB02_CHECKED_PROVIDER = 3
};

static inline int mb02_ranges_overlap(uintptr_t left, size_t left_bytes,
                                      uintptr_t right, size_t right_bytes) {
    /* The caller has already refused address-range arithmetic overflow. */
    return left < right + right_bytes && right < left + left_bytes;
}

/* Correctness-only adapter for the existing 32-bit limb kernel ABI.
 * Capacities are caller-declared uint32_t limb counts, not inferred object
 * sizes. Storage must designate actual live objects covering those counts.
 * The void-pointer admission boundary rejects misalignment before converting
 * storage to typed pointers. On the supported flat-address targets uintptr_t
 * intervals check active input/output overlap; the two read-only inputs may
 * alias, including a square. Zero has a positive stored length and zero limbs.
 *
 * A successful call writes every na+nb output limb. Failure leaves out_storage
 * untouched, but a faulty provider may already have changed an input. Two
 * prefills and adjacent guards detect omitted stores and bounded overwrites;
 * arbitrary memory corruption still requires sanitizers. This is not an
 * arithmetic oracle: callers must independently verify the returned value.
 * Never place this adapter inside an arithmetic-only timing interval.
 */
static inline int mb02_checked_product(
    mb02_multiply_fn multiply,
    const void *a_storage, size_t na, size_t a_capacity,
    const void *b_storage, size_t nb, size_t b_capacity,
    void *out_storage, size_t out_capacity) {
    if (!multiply || !a_storage || !b_storage || !out_storage || !na || !nb ||
        na > a_capacity || nb > b_capacity ||
        nb > SIZE_MAX / sizeof(uint32_t) - 2 ||
        na > SIZE_MAX / sizeof(uint32_t) - 2 - nb)
        return MB02_CHECKED_ADMISSION;
    size_t count = na + nb;
    if (count > out_capacity) return MB02_CHECKED_ADMISSION;
    size_t abytes = na * sizeof(uint32_t), bbytes = nb * sizeof(uint32_t);
    size_t obytes = count * sizeof(uint32_t);
    uintptr_t a_address = (uintptr_t)a_storage;
    uintptr_t b_address = (uintptr_t)b_storage;
    uintptr_t out_address = (uintptr_t)out_storage;
    if (a_address % _Alignof(uint32_t) || b_address % _Alignof(uint32_t) ||
        out_address % _Alignof(uint32_t) ||
        a_address > UINTPTR_MAX - abytes ||
        b_address > UINTPTR_MAX - bbytes ||
        out_address > UINTPTR_MAX - obytes ||
        mb02_ranges_overlap(a_address, abytes, out_address, obytes) ||
        mb02_ranges_overlap(b_address, bbytes, out_address, obytes))
        return MB02_CHECKED_ADMISSION;

    const uint32_t *a = a_storage, *b = b_storage;
    uint32_t *out = out_storage;
    uint32_t *saved_a = malloc(abytes), *saved_b = malloc(bbytes);
    uint32_t *guarded = malloc((count + 2) * sizeof(*guarded));
    uint32_t *first = malloc(obytes);
    if (!saved_a || !saved_b || !guarded || !first) {
        free(saved_a); free(saved_b); free(guarded); free(first);
        return MB02_CHECKED_ALLOCATION;
    }
    memcpy(saved_a, a, abytes);
    memcpy(saved_b, b, bbytes);
    int status = MB02_CHECKED_OK;
    for (int pass = 0; pass < 2; ++pass) {
        guarded[0] = UINT32_C(0xc0deface);
        guarded[count + 1] = UINT32_C(0x5afe0123);
        memset(guarded + 1, pass ? 0x5a : 0xa5, obytes);
        if (multiply(a, na, b, nb, guarded + 1) != 0 ||
            guarded[0] != UINT32_C(0xc0deface) ||
            guarded[count + 1] != UINT32_C(0x5afe0123) ||
            memcmp(a, saved_a, abytes) || memcmp(b, saved_b, bbytes)) {
            status = MB02_CHECKED_PROVIDER;
            break;
        }
        if (pass == 0) memcpy(first, guarded + 1, obytes);
        else if (memcmp(first, guarded + 1, obytes))
            status = MB02_CHECKED_PROVIDER;
    }
    if (status == MB02_CHECKED_OK) memcpy(out, first, obytes);
    free(saved_a); free(saved_b); free(guarded); free(first);
    return status;
}

#endif
