#include "composition.h"
#include "numerical_internal.h"
#include <stdlib.h>

#define TRY(operation) do { status=(operation); if (status!=EXACT_OK) goto done; } while (0)

enum { TARGET_RECORDS=32, AXES=2, TENSOR_LINE_SLOTS=2*64, COMPRESSION_TEMPORARIES=3*5 };

struct composition_source_plan {
  composition_parameters parameters;
  composition_chirp_plan *chirp;
  resampling_plan *axes[AXES];
  exact_gaussian *expanded, *transformed;
};

exact_status composition_source_prepare(uint64_t record_budget,composition_source_plan **out) {
  if (!out || *out) return EXACT_INVALID;
  composition_source_plan *plan=calloc(1,sizeof(*plan));
  if (!plan) return EXACT_ALLOCATION;
  recovery_certificate certificate;
  exact_status status;
  TRY(composition_admit(REDUCED_ALGEBRA,&plan->parameters,&certificate));
  const composition_parameters *parameters=&plan->parameters;
  resampling_parameters shape[AXES];
  uint64_t coefficients[AXES];
  uint64_t reserved=3*TARGET_RECORDS+TENSOR_LINE_SLOTS+COMPRESSION_TEMPORARIES;
  for (unsigned axis=0;axis<AXES;++axis) {
    TRY(resampling_admit(parameters->precision,parameters->source[axis],
                        parameters->target[axis],parameters->alpha,record_budget,&shape[axis]));
    coefficients[axis]=2*(uint64_t)shape[axis].gaussian_radius*shape[axis].target_length
      +2*(uint64_t)shape[axis].error_radius*shape[axis].source_length+shape[axis].source_length;
    reserved+=coefficients[axis];
  }
  if (record_budget<reserved) { status=EXACT_CAPACITY; goto done; }
  TRY(composition_chirp_prepare(record_budget-reserved,&plan->chirp));
  for (unsigned axis=0;axis<AXES;++axis)
    TRY(resampling_prepare(&shape[axis],coefficients[axis],&plan->axes[axis]));
  size_t capacity=(parameters->precision+32)/32;
  TRY(numerical_values(&plan->expanded,TARGET_RECORDS,capacity));
  TRY(numerical_values(&plan->transformed,TARGET_RECORDS,capacity));
  *out=plan;
  return EXACT_OK;
done:
  composition_source_clear(plan);
  return status;
}

void composition_source_clear(composition_source_plan *plan) {
  if (!plan) return;
  composition_chirp_clear(plan->chirp);
  for (unsigned axis=0;axis<AXES;++axis) resampling_clear(plan->axes[axis]);
  numerical_clear_values(plan->expanded,TARGET_RECORDS);
  numerical_clear_values(plan->transformed,TARGET_RECORDS);
  free(plan);
}

exact_status composition_source_transform(composition_source_plan *plan,const exact_gaussian *input,size_t input_length,
                                           exact_gaussian *out,size_t output_length,int opposite,
                                           composition_trace *trace,composition_observer observer,void *context) {
  if (!plan || input_length!=TARGET_RECORDS || output_length!=TARGET_RECORDS || !trace
      || (opposite!=0 && opposite!=1)) return EXACT_INVALID;
  exact_status status=numerical_array_contract(input,NULL,out,TARGET_RECORDS);
  if (status!=EXACT_OK) return status;
  if (exact_overlap(trace,sizeof(*trace),input,input_length*sizeof(*input))
      || exact_overlap(trace,sizeof(*trace),out,output_length*sizeof(*out))) return EXACT_INVALID;
  const composition_parameters *parameters=&plan->parameters;
  for (unsigned index=0;index<TARGET_RECORDS;++index) {
    const exact_dyadic *parts[]={&input[index].real,&input[index].imaginary,&out[index].real,&out[index].imaginary};
    for (unsigned part=0;part<4;++part)
      if (exact_overlap(trace,sizeof(*trace),parts[part]->limbs,parts[part]->capacity*sizeof(uint32_t)))
        return EXACT_INVALID;
    status=numerical_radius(&input[index],parameters->precision,1);
    if (status!=EXACT_OK) return status;
    unsigned row=index/parameters->target[1],column=index%parameters->target[1];
    if ((row>=parameters->source[0] || column>=parameters->source[1])
        && (input[index].real.length || input[index].imaginary.length)) return EXACT_INVALID;
  }

  resampling_parameters axis_shape;
  resampling_work before[AXES];
  composition_trace receipt={0}, chirp={0};
  for (unsigned axis=0;axis<AXES;++axis) {
    status=resampling_receipt(plan->axes[axis],&axis_shape,&before[axis]);
    if (status!=EXACT_OK) return status;
  }
  for (unsigned index=0;index<TARGET_RECORDS;++index) {
    TRY(numerical_copy_value(&plan->expanded[index],&input[index]));
    if (opposite) plan->expanded[index].imaginary.sign=-plan->expanded[index].imaginary.sign;
  }
  TRY(resampling_tensor(plan->axes[0],plan->axes[1],0,1,1,parameters->target[0],parameters->target[1],
                        plan->expanded,TARGET_RECORDS));
  ++receipt.stages;
  if (observer) TRY(observer(context,"source-expand",plan->expanded,TARGET_RECORDS));

  status=composition_chirp(plan->chirp,plan->expanded,TARGET_RECORDS,
                          plan->transformed,TARGET_RECORDS,&chirp,NULL,NULL);
  receipt.stages+=chirp.stages;
  if (status!=EXACT_OK) goto done;
  if (observer) TRY(observer(context,"source-chirp",plan->transformed,TARGET_RECORDS));

  TRY(resampling_tensor(plan->axes[0],plan->axes[1],1,1,1,parameters->target[0],parameters->target[1],
                        plan->transformed,TARGET_RECORDS));
  ++receipt.stages;
  if (observer) TRY(observer(context,"source-compress",plan->transformed,TARGET_RECORDS));

  unsigned scale=parameters->source_scale_bits;
#ifdef MUTANT_SOURCE_SCALE
  --scale;
#endif
  for (unsigned index=0;index<TARGET_RECORDS;++index) {
    exact_gaussian *value=&plan->transformed[index];
    TRY(exact_shift(&value->real,&value->real,scale));
    TRY(exact_shift(&value->imaginary,&value->imaginary,scale));
    if (opposite) value->imaginary.sign=-value->imaginary.sign;
    TRY(numerical_radius(value,parameters->precision,0));
    if (value->real.length>out[index].real.capacity || value->imaginary.length>out[index].imaginary.capacity) {
      status=EXACT_CAPACITY;
      goto done;
    }
  }
  ++receipt.stages;
  if (observer) TRY(observer(context,"source-scale",plan->transformed,TARGET_RECORDS));
  for (unsigned index=0;index<TARGET_RECORDS;++index)
    TRY(numerical_copy_value(&out[index],&plan->transformed[index]));
done:
  receipt.motif_calls=chirp.motif_calls;
  receipt.short_calls=chirp.short_calls;
  receipt.scalar_products=chirp.scalar_products;
  receipt.prepared_matrix_entries=chirp.prepared_matrix_entries;
  receipt.allocated_limb_bytes=chirp.allocated_limb_bytes
    +4*TARGET_RECORDS*plan->expanded[0].real.capacity*sizeof(uint32_t);
  receipt.allocated_descriptor_bytes=chirp.allocated_descriptor_bytes
    +sizeof(*plan)+2*TARGET_RECORDS*sizeof(exact_gaussian);
  for (unsigned axis=0;axis<AXES;++axis) {
    resampling_work after;
    if (resampling_receipt(plan->axes[axis],&axis_shape,&after)!=EXACT_OK) continue;
    receipt.allocated_limb_bytes+=after.allocated_limb_bytes;
    receipt.allocated_descriptor_bytes+=after.allocated_descriptor_bytes;
    receipt.scalar_products+=after.ram_products-before[axis].ram_products;
    receipt.line_calls+=after.line_calls-before[axis].line_calls;
  }
  *trace=receipt;
  return status;
}
