/* Bounded observer of the retained MB05 provider; no multiplier lives here. */
#include "recursive_trace.h"
#include "../providers/providers.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef MB17_TRACE
static size_t sequence, next_node, depth, stack[8], parents[32];
static int trace_failure;

static void words(const uint32_t *value, size_t count) {
    for (size_t index=0; index<count; ++index)
        printf(index ? ",%08" PRIx32 : "%08" PRIx32, value[index]);
}

static void event(const char *kind,size_t node,size_t width,const char *units) {
    if (sequence >= 128 || node >= 32) { trace_failure=1; return; }
    printf("%zu\t%s\t%zu\t",sequence++,kind,node);
    if (parents[node] == SIZE_MAX) putchar('-'); else printf("%zu",parents[node]);
    printf("\t%zu\t32\t%s\t",width,units);
}

size_t mb17_recursive_enter(const uint32_t *a,const uint32_t *b,size_t n) {
    size_t node=next_node++;
    if (node >= 32 || depth >= 8) { trace_failure=1; return 31; }
    parents[node]=depth ? stack[depth-1] : SIZE_MAX;
    stack[depth++]=node;
    event("enter",node,n,"limbs"); words(a,n); putchar('|'); words(b,n); putchar('\n');
    return node;
}

void mb17_recursive_scratch(size_t node,const char *kind,size_t count) {
    event(kind,node,count,"limbs"); puts("scratch");
}

void mb17_recursive_difference(size_t node,const uint32_t *a,const uint32_t *b,size_t n) {
    event("difference",node,n,"limbs"); words(a,n); putchar('|'); words(b,n); putchar('\n');
}

void mb17_recursive_leaf(size_t node,size_t n) {
    event("schoolbook-call",node,n,"limb-pairs"); printf("%zu\n",n*n);
}

void mb17_recursive_leave(size_t node,const uint32_t *out,size_t n,int status) {
    if (!depth || stack[depth-1] != node || status) trace_failure=1;
    event("leave",node,n,"limbs");
    if (!status) words(out,2*n); else fputs("ERROR",stdout);
    putchar('\n');
    if (depth) --depth;
}
#endif

static int number(const char *text,size_t maximum,size_t *result) {
    if (!*text || *text=='-' || *text=='+') return 1;
    char *end; errno=0;
    unsigned long long value=strtoull(text,&end,10);
    if (errno || *end || value>maximum) return 1;
    *result=(size_t)value; return 0;
}

int main(int argc,char **argv) {
    if (argc<10) return 2;
    size_t na,nb;
    if (number(argv[6],64,&na) || number(argv[7],64,&nb) || !na || !nb ||
        (size_t)argc!=8+na+nb) return 2;
    uint32_t a[64],b[64],out[128];
    for (size_t index=0; index<na+nb; ++index) {
        size_t value;
        if (number(argv[8+index],UINT32_MAX,&value)) return 2;
        if (index<na) a[index]=(uint32_t)value; else b[index-na]=(uint32_t)value;
    }
#ifdef MB17_TRACE
    printf("recursive-trace-v1\nprovider_source_sha256\t%s\ndriver_source_sha256\t%s\nsource_manifest_sha256\t%s\nbinary_sha256\t%s\ncase_sha256\t%s\n",argv[1],argv[2],argv[5],argv[3],argv[4]);
    puts("profile\texecuted-karatsuba-control\ncoordinate_space\tlogical-call-and-limb-index\nmaximum_operand_limbs\t64\ncomplete\t1");
    puts("seq\tkind\tnode\tparent\twidth\tword_bits\tunits\tpayload");
#endif
    int status=mb05_karatsuba32(a,na,b,nb,out);
    if (status) return status;
#ifdef MB17_TRACE
    if (trace_failure || depth) return 4;
#endif
    fputs("product\t",stdout);
    for (size_t index=0; index<na+nb; ++index)
        printf(index ? ",%08" PRIx32 : "%08" PRIx32,out[index]);
    putchar('\n');
    return ferror(stdout) ? 3 : 0;
}
