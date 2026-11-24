#ifndef OPENAI2026_COMPOSITION_H
#define OPENAI2026_COMPOSITION_H
#include "numerical.h"
#include "error_bounds.h"
typedef enum { REDUCED_ALGEBRA, PUBLISHED_FIXED } composition_profile;
typedef struct {
  unsigned precision,radix_bits,input_bit_limit,axes,ring_length;
  unsigned source[2],target[2],prefix[2],inverse_prefix[2];
  unsigned source_product,target_product,outer_records,alpha,source_scale_bits;
  unsigned prime_candidates,trial_divisions;
} composition_parameters;
typedef struct {
  uint32_t error_units[8]; /* unsigned LSF magnitude, denominator 2^precision */
  unsigned error_words,precision,strict_half_unit,coefficient_limit;
  unsigned chirp_error_units,source_error_base,source_error_shift;
} recovery_certificate;
/* Fixed, explicit reduced model. The eight-bit declared input bound includes
 * leading zeroes. Published-fixed is unsupported and explicitly refuses. */
exact_status composition_admit(composition_profile,composition_parameters *,recovery_certificate *);
typedef struct composition_context composition_context;
typedef struct {
  uint64_t stages,crt_moves,motif_calls,short_calls,scalar_products,line_calls;
  uint64_t prepared_matrix_entries,allocated_limb_bytes,allocated_descriptor_bytes;
} composition_trace;
typedef exact_status (*composition_observer)(void *,const char *,const exact_gaussian *,size_t);
/* Explicit padding plus descending triangular CRT rotations, or their
 * ascending inverse and padding deletion. Non-disk payloads are allowed here. */
exact_status composition_crt(const composition_parameters *,const exact_gaussian *,size_t,
                             exact_gaussian *,size_t,int inverse,composition_trace *,composition_observer,void *);
typedef struct composition_chirp_plan composition_chirp_plan;
/* Fixed reduced 4 by 8 target. The plan owns phase and motif storage. Budget
 * counts Gaussian record slots, including the ring convolution temporaries.
 * Input and output are disjoint 32-entry disk-grid arrays. Calls are sequential
 * and preserve output on failure, including an observer refusal. */
exact_status composition_chirp_prepare(uint64_t record_budget,composition_chirp_plan **);
void composition_chirp_clear(composition_chirp_plan *);
exact_status composition_chirp(composition_chirp_plan *,const exact_gaussian *,size_t,
                               exact_gaussian *,size_t,composition_trace *,composition_observer,void *);
typedef struct composition_source_plan composition_source_plan;
/* Reduced 3 by 5 source in a 4 by 8 row-major box. Input and output use the
 * p-bit disk grid and must be disjoint. The input norm is at most one half.
 * Both directions preserve source padding. The observer sees four outer
 * stages. The receipt counts the five chirp stages in place of its single
 * outer chirp event.
 * The budget covers tensor, coefficient and chirp record slots. Calls use
 * the plan sequentially, and failures leave the caller's output untouched. */
exact_status composition_source_prepare(uint64_t record_budget,composition_source_plan **);
void composition_source_clear(composition_source_plan *);
exact_status composition_source_transform(composition_source_plan *,const exact_gaussian *,size_t,
                                           exact_gaussian *,size_t,int opposite,composition_trace *,
                                           composition_observer,void *);
exact_status composition_prepare(composition_profile,uint64_t record_budget,composition_context **);
void composition_clear(composition_context *);
/* ASCII MSB-first bits, lengths 1..8; output is exactly twice the larger
 * declared length, including zero padding. Arrays and trace must be disjoint. */
exact_status multiply_exact(composition_context *,const unsigned char *,size_t,
                            const unsigned char *,size_t,unsigned char *,size_t,composition_trace *);
#endif
