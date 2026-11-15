/* Reusable direct-call acceptance example for checked_product.h.
 * Link a kernel implementing the unchanged mul_schoolbook ABI. The ordinary
 * invocation checks valid calls and pre-call refusals; named defect invocations
 * must fail the outer process. Exact expected products remain independent of
 * the guard, which checks memory/protocol behavior rather than arithmetic.
 */
#include <stdio.h>
#include <string.h>
#include "checked_product.h"

extern int mul_schoolbook(const uint32_t *, size_t,
                          const uint32_t *, size_t, uint32_t *);

static size_t provider_calls;
static const char *defect;

static int observed_provider(const uint32_t *a, size_t na,
                              const uint32_t *b, size_t nb, uint32_t *out) {
    ++provider_calls;
    if (defect && !strcmp(defect, "no-write")) return 0;
    if (defect && !strcmp(defect, "wrong-answer")) {
        memset(out, 0, (na + nb) * sizeof(*out));
        return 0;
    }
    if (defect && !strcmp(defect, "omit-upper-zero")) {
        uint32_t product[3];
        if (na != 2 || nb != 1) return 90;
        int status = mul_schoolbook(a, na, b, nb, product);
        if (!status) memcpy(out, product, (na + nb - 1) * sizeof(*out));
        return status;
    }
    int status = mul_schoolbook(a, na, b, nb, out);
    if (defect && !strcmp(defect, "before-output")) out[-1] = 0;
    if (defect && !strcmp(defect, "after-output")) out[na + nb] = 0;
    if (defect && !strcmp(defect, "alter-input")) ((uint32_t *)a)[0] ^= 1;
    if (defect && !strcmp(defect, "status-after-output")) return 7;
    return status;
}

static int admission_checks(void) {
    uint32_t a[8] = {1}, b[8] = {2}, out[8];
    _Alignas(uint32_t) unsigned char bytes[sizeof(uint32_t) * 8 + 1];
    memset(out, 0xa5, sizeof(out));
    provider_calls = 0;
    size_t refusals = 0;
#define REFUSE(...) do { \
    if (mb02_checked_product(__VA_ARGS__) != MB02_CHECKED_ADMISSION || \
        provider_calls != 0) return __LINE__; \
    ++refusals; \
} while (0)
    REFUSE(NULL, a, 1, 8, b, 1, 8, out, 8);
    REFUSE(observed_provider, NULL, 1, 8, b, 1, 8, out, 8);
    REFUSE(observed_provider, a, 1, 8, NULL, 1, 8, out, 8);
    REFUSE(observed_provider, a, 1, 8, b, 1, 8, NULL, 8);
    REFUSE(observed_provider, a, 0, 8, b, 1, 8, out, 8);
    REFUSE(observed_provider, a, 1, 8, b, 0, 8, out, 8);
    REFUSE(observed_provider, a, 2, 1, b, 1, 8, out, 8);
    REFUSE(observed_provider, a, 1, 8, b, 2, 1, out, 8);
    REFUSE(observed_provider, a, 1, 8, b, 1, 8, out, 1);
    REFUSE(observed_provider, a, SIZE_MAX, SIZE_MAX, b, 1, 8, out, SIZE_MAX);
    REFUSE(observed_provider, a, 1, 8, b, SIZE_MAX, SIZE_MAX, out, SIZE_MAX);
    REFUSE(observed_provider, a, SIZE_MAX / sizeof(*a) - 1, SIZE_MAX,
           b, 1, 8, out, SIZE_MAX);
    REFUSE(observed_provider, a, 2, 8, b, 1, 8, a, 8);
    REFUSE(observed_provider, a, 2, 8, b, 1, 8, a + 1, 7);
    REFUSE(observed_provider, a, 1, 8, b, 2, 8, b, 8);
    REFUSE(observed_provider, a, 1, 8, b, 2, 8, b + 1, 7);
    if (_Alignof(uint32_t) > 1) {
        REFUSE(observed_provider, bytes + 1, 1, 8, b, 1, 8, out, 8);
        REFUSE(observed_provider, a, 1, 8, bytes + 1, 1, 8, out, 8);
        REFUSE(observed_provider, a, 1, 8, b, 1, 8, bytes + 1, 8);
    }
#undef REFUSE
    for (size_t i = 0; i < 8; ++i)
        if (out[i] != UINT32_C(0xa5a5a5a5)) return 91;
    printf("admission_refusals\t%zu\n", refusals);
    return 0;
}

static int product_checks(void) {
    uint32_t a[2] = {1, 0}, b[1] = {2};
    uint32_t out[4];
    memset(out, 0xa5, sizeof(out));
    provider_calls = 0;
    int status = mb02_checked_product(observed_provider,
                                      a, 2, 2, b, 1, 1, out, 4);
    if (status) {
        /* The caller's output is not published on a provider refusal. */
        for (size_t i = 0; i < 4; ++i)
            if (out[i] != UINT32_C(0xa5a5a5a5)) return 92;
        return status;
    }
    if (provider_calls != 2 || out[0] != 2 || out[1] || out[2] ||
        out[3] != UINT32_C(0xa5a5a5a5)) return 93;
    if (a[0] != 1 || a[1] || b[0] != 2) return 94;
    /* If a defect escaped both the guard and exact-value check, report success
     * here so the outer negative-control test fails. Naming a defect is not
     * itself a reason to reject its process. */
    if (defect) return 0;

    /* An aliased read-only input is a square, with the maximum carry chain. */
    a[0] = a[1] = UINT32_MAX;
    provider_calls = 0;
    status = mb02_checked_product(observed_provider,
                                  a, 2, 2, a, 2, 2, out, 4);
    if (status || provider_calls != 2 || out[0] != 1 || out[1] != 0 ||
        out[2] != UINT32_MAX - 1 || out[3] != UINT32_MAX) return 96;
    if (a[0] != UINT32_MAX || a[1] != UINT32_MAX) return 97;

    /* Stored zero is one zero limb; all declared output limbs are written. */
    a[0] = 0;
    memset(out, 0xa5, sizeof(out));
    status = mb02_checked_product(observed_provider,
                                  a, 1, 2, b, 1, 1, out, 4);
    if (status || out[0] || out[1] ||
        out[2] != UINT32_C(0xa5a5a5a5)) return 98;
    return 0;
}

static int low_level_checks(void) {
    uint32_t a[2] = {UINT32_MAX, UINT32_MAX};
    uint32_t b[2] = {UINT32_MAX, UINT32_MAX}, out[4];
    if (!mul_schoolbook(NULL, 1, b, 1, out) ||
        !mul_schoolbook(a, 1, NULL, 1, out) ||
        !mul_schoolbook(a, 1, b, 1, NULL) ||
        !mul_schoolbook(a, 0, b, 1, out) ||
        !mul_schoolbook(a, SIZE_MAX, b, 1, out) ||
        !mul_schoolbook(a, 1, b, SIZE_MAX, out)) return 100;
    if (mul_schoolbook(a, 2, b, 2, out) ||
        out[0] != 1 || out[1] || out[2] != UINT32_MAX - 1 ||
        out[3] != UINT32_MAX || a[0] != UINT32_MAX || a[1] != UINT32_MAX ||
        b[0] != UINT32_MAX || b[1] != UINT32_MAX) return 101;
    a[0] = 1; a[1] = 0; b[0] = 2;
    memset(out, 0xa5, sizeof(out));
    if (mul_schoolbook(a, 2, b, 1, out) || out[0] != 2 || out[1] || out[2])
        return 102;
    return 0;
}

int main(int argc, char **argv) {
    if (argc > 2) return 99;
    defect = argc == 2 ? argv[1] : NULL;
    int status = defect ? 0 : low_level_checks();
    if (!status && !defect) status = admission_checks();
    if (!status) status = product_checks();
    if (status) {
        fprintf(stderr, "MB02 direct-call refusal: %s status %d\n",
                defect ? defect : "fixture", status);
        return status;
    }
    puts("MB02_DIRECT_CALL_GUARD\tPASS");
    return 0;
}
