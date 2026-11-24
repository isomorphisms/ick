#include "composition.h"
#include "certified_internal.h"
#include "numerical_internal.h"
#include <stdlib.h>

#define TRY(operation) do { status=(operation); if (status!=EXACT_OK) goto done; } while (0)

enum { TARGET_RECORDS = 32, RING_LENGTH = 8, RING_TEMPORARY_RECORDS = 4 * TARGET_RECORDS };

struct composition_chirp_plan {
  composition_parameters parameters;
  certified_scalar_context scalar;
  synthetic_plan *ring;
  numerical_trace ring_preparation;
  exact_gaussian *phase, *twist, *kernel, *first, *second;
  directed_interval products, sums;
};

static exact_status phase_product(composition_chirp_plan *plan,exact_gaussian *out,
                                  const exact_gaussian *input,const exact_gaussian *phase,
                                  int conjugate,int half) {
  exact_dyadic *ac=&plan->products.lower, *bd=&plan->products.upper;
  exact_dyadic *real=&plan->sums.lower, *imaginary=&plan->sums.upper;
  certified_scalar_context *scalar=&plan->scalar;
  exact_status status;
  TRY(scalar_point_product(scalar,ac,&input->real,&phase->real));
  TRY(scalar_point_product(scalar,bd,&input->imaginary,&phase->imaginary));
  if (conjugate) bd->sign=-bd->sign;
  TRY(exact_subtract(real,ac,bd));
  TRY(scalar_point_product(scalar,ac,&input->real,&phase->imaginary));
  TRY(scalar_point_product(scalar,bd,&input->imaginary,&phase->real));
  if (conjugate) ac->sign=-ac->sign;
  TRY(exact_add(imaginary,ac,bd));
  if (half) {
    TRY(exact_half(real,real));
    TRY(exact_half(imaginary,imaginary));
  }
  TRY(exact_truncate(&out->real,real,plan->parameters.precision));
  TRY(exact_truncate(&out->imaginary,imaginary,plan->parameters.precision));
done:
  return status;
}

static unsigned chirp_numerator(const composition_parameters *parameters,unsigned index) {
  unsigned row=index/parameters->ring_length, column=index%parameters->ring_length;
  unsigned modulus=2*parameters->ring_length;
  unsigned sum=parameters->source[0]*row*row*(parameters->ring_length/parameters->target[0])
    +parameters->source[1]*column*column*(parameters->ring_length/parameters->target[1]);
  return (modulus-sum%modulus)%modulus;
}

exact_status composition_chirp_prepare(uint64_t record_budget,composition_chirp_plan **out) {
  const uint64_t reserved=4*TARGET_RECORDS+RING_LENGTH+RING_TEMPORARY_RECORDS;
  if (!out || *out) return EXACT_INVALID;
  if (record_budget<reserved) return EXACT_CAPACITY;
  composition_chirp_plan *plan=calloc(1,sizeof(*plan));
  if (!plan) return EXACT_ALLOCATION;
  recovery_certificate certificate;
  exact_status status;
  TRY(composition_admit(REDUCED_ALGEBRA,&plan->parameters,&certificate));
  const composition_parameters *parameters=&plan->parameters;
  TRY(synthetic_prepare(parameters->precision,parameters->ring_length,parameters->target,
                        parameters->axes-1,record_budget-reserved,&plan->ring));
  TRY(synthetic_warm(plan->ring,&plan->ring_preparation));
  TRY(certified_scalar_init(&plan->scalar,parameters->precision));
  TRY(bound_init(&plan->scalar,&plan->products));
  TRY(bound_init(&plan->scalar,&plan->sums));
  size_t capacity=(parameters->precision+32)/32;
  TRY(numerical_values(&plan->phase,TARGET_RECORDS,capacity));
  TRY(numerical_values(&plan->twist,RING_LENGTH,capacity));
  TRY(numerical_values(&plan->kernel,TARGET_RECORDS,capacity));
  TRY(numerical_values(&plan->first,TARGET_RECORDS,capacity));
  TRY(numerical_values(&plan->second,TARGET_RECORDS,capacity));
  scalar_certificate phase_certificate;
  for (unsigned column=0;column<RING_LENGTH;++column)
    TRY(certified_root(&plan->scalar,column,2*RING_LENGTH,&plan->twist[column],&phase_certificate));
  for (unsigned index=0;index<TARGET_RECORDS;++index) {
    TRY(certified_root(&plan->scalar,chirp_numerator(parameters,index),2*RING_LENGTH,
                       &plan->phase[index],&phase_certificate));
    /* The ring implementation admits two half-disk operands. Each half is
     * taken before rounding, and the convolution is restored by four. */
    TRY(phase_product(plan,&plan->kernel[index],&plan->phase[index],
                      &plan->twist[index%RING_LENGTH],0,1));
  }
  *out=plan;
  return EXACT_OK;
done:
  composition_chirp_clear(plan);
  return status;
}

void composition_chirp_clear(composition_chirp_plan *plan) {
  if (!plan) return;
  numerical_clear_values(plan->phase,TARGET_RECORDS);
  numerical_clear_values(plan->twist,RING_LENGTH);
  numerical_clear_values(plan->kernel,TARGET_RECORDS);
  numerical_clear_values(plan->first,TARGET_RECORDS);
  numerical_clear_values(plan->second,TARGET_RECORDS);
  bound_clear(&plan->scalar,&plan->products);
  bound_clear(&plan->scalar,&plan->sums);
  certified_scalar_clear(&plan->scalar);
  synthetic_clear(plan->ring);
  free(plan);
}

exact_status composition_chirp(composition_chirp_plan *plan,const exact_gaussian *input,size_t input_length,
                               exact_gaussian *out,size_t output_length,composition_trace *trace,
                               composition_observer observer,void *context) {
  if (!plan || input_length!=TARGET_RECORDS || output_length!=TARGET_RECORDS || !trace)
    return EXACT_INVALID;
  exact_status status=numerical_array_contract(input,NULL,out,TARGET_RECORDS);
  if (status!=EXACT_OK) return status;
  if (exact_overlap(trace,sizeof(*trace),input,input_length*sizeof(*input))
      || exact_overlap(trace,sizeof(*trace),out,output_length*sizeof(*out))) return EXACT_INVALID;
  unsigned precision=plan->parameters.precision;
  for (unsigned index=0;index<TARGET_RECORDS;++index) {
    const exact_dyadic *components[]={&input[index].real,&input[index].imaginary,&out[index].real,&out[index].imaginary};
    for (unsigned part=0;part<4;++part)
      if (exact_overlap(trace,sizeof(*trace),components[part]->limbs,
                        components[part]->capacity*sizeof(uint32_t))) return EXACT_INVALID;
    status=numerical_radius(&input[index],precision,0);
    if (status!=EXACT_OK) return status;
  }

  composition_trace receipt={0};
  numerical_trace ring_trace={0};
  uint64_t first_product=plan->scalar.work.products;
  for (unsigned index=0;index<TARGET_RECORDS;++index)
    TRY(phase_product(plan,&plan->first[index],&input[index],&plan->phase[index],1,0));
  ++receipt.stages;
  if (observer) TRY(observer(context,"chirp-input-phase",plan->first,TARGET_RECORDS));

  for (unsigned index=0;index<TARGET_RECORDS;++index)
    TRY(phase_product(plan,&plan->second[index],&plan->first[index],&plan->twist[index%RING_LENGTH],0,1));
  ++receipt.stages;
  if (observer) TRY(observer(context,"chirp-half-twist",plan->second,TARGET_RECORDS));

  const short_mul_provider provider={"reduced-schoolbook-four-packed-products",short_mul_schoolbook};
  TRY(numerical_convolution(plan->ring,plan->kernel,plan->second,plan->first,&provider,&ring_trace));
  if (ring_trace.error_binary_scale!=2*precision || ring_trace.error_squared_numerator>=40*40) {
    status=EXACT_INVALID;
    goto done;
  }
  ++receipt.stages;
  if (observer) TRY(observer(context,"chirp-ring-convolution",plan->first,TARGET_RECORDS));

  for (unsigned index=0;index<TARGET_RECORDS;++index) {
#ifdef MUTANT_CHIRP_TWIST_SIGN
    int inverse_twist=0;
#else
    int inverse_twist=1;
#endif
    TRY(phase_product(plan,&plan->second[index],&plan->first[index],&plan->twist[index%RING_LENGTH],inverse_twist,0));
    TRY(exact_shift(&plan->second[index].real,&plan->second[index].real,2));
    TRY(exact_shift(&plan->second[index].imaginary,&plan->second[index].imaginary,2));
  }
  ++receipt.stages;
  if (observer) TRY(observer(context,"chirp-untwist-restore",plan->second,TARGET_RECORDS));

  for (unsigned index=0;index<TARGET_RECORDS;++index) {
    TRY(phase_product(plan,&plan->first[index],&plan->second[index],&plan->phase[index],1,0));
    TRY(numerical_radius(&plan->first[index],precision,0));
    if (plan->first[index].real.length>out[index].real.capacity
        || plan->first[index].imaginary.length>out[index].imaginary.capacity) {
      status=EXACT_CAPACITY;
      goto done;
    }
  }
  ++receipt.stages;
  if (observer) TRY(observer(context,"chirp-output-phase",plan->first,TARGET_RECORDS));
  for (unsigned index=0;index<TARGET_RECORDS;++index)
    TRY(numerical_copy_value(&out[index],&plan->first[index]));

done:
  receipt.motif_calls=ring_trace.motif_calls;
  receipt.short_calls=ring_trace.short_calls;
  receipt.scalar_products=plan->scalar.work.products-first_product;
  receipt.prepared_matrix_entries=plan->ring_preparation.prepared_matrix_entries;
  size_t slots=4*TARGET_RECORDS+RING_LENGTH;
  receipt.allocated_limb_bytes=ring_trace.persistent_limb_bytes
    +slots*2*plan->first[0].real.capacity*sizeof(uint32_t)+plan->scalar.work.peak_limb_bytes;
  receipt.allocated_descriptor_bytes=ring_trace.persistent_descriptor_bytes
    +sizeof(*plan)+slots*sizeof(exact_gaussian);
  *trace=receipt;
  return status;
}
