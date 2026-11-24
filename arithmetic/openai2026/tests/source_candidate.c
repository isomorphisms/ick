#include "composition.h"
#include "numerical_internal.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { RECORDS=32, PRECISION=256, WORD_BYTES=33, CAPACITY=9 };

static exact_status fixture(exact_gaussian *value,unsigned example,unsigned index) {
  int64_t real=0, imaginary=0;
  if (index/8<3 && index%8<5) {
    switch (example) {
    case 1: real=index==0 ? 8 : 0; break;
    case 2: imaginary=index==12 ? -8 : 0; break;
    case 3: real=index==20 ? 6 : 0; imaginary=index==20 ? 4 : 0; break;
    case 4: real=(int64_t)(index%7)-3; imaginary=(int64_t)((index*5)%11)-5; break;
    }
  }
  exact_status status=exact_set(&value->real,real,4);
  return status==EXACT_OK ? exact_set(&value->imaginary,imaginary,4) : status;
}

static exact_status emit(unsigned example,int opposite,const exact_gaussian *values) {
  for (unsigned index=0;index<RECORDS;++index) {
    unsigned char real[WORD_BYTES], imaginary[WORD_BYTES];
    if (exact_serialize(&values[index].real,PRECISION,real,WORD_BYTES)
        || exact_serialize(&values[index].imaginary,PRECISION,imaginary,WORD_BYTES)) return EXACT_INVALID;
    printf("source\t%u\t%d\t%u\t",example,opposite,index);
    for (unsigned byte=0;byte<WORD_BYTES;++byte) printf("%02x",real[byte]);
    putchar('\t');
    for (unsigned byte=0;byte<WORD_BYTES;++byte) printf("%02x",imaginary[byte]);
    putchar('\n');
  }
  return ferror(stdout) ? EXACT_INVALID : EXACT_OK;
}

typedef struct { unsigned stages; int refuse; } observed_stages;
static exact_status observe(void *opaque,const char *stage,const exact_gaussian *values,size_t length) {
  static const char *const names[]={"source-expand","source-chirp","source-compress","source-scale"};
  observed_stages *observed=opaque;
  if (!values || length!=RECORDS || observed->stages>=sizeof(names)/sizeof(names[0])
      || strcmp(names[observed->stages],stage)) return EXACT_INVALID;
  ++observed->stages;
  return observed->refuse && observed->stages==4 ? EXACT_INVALID : EXACT_OK;
}

int main(void) {
  composition_source_plan *plan=NULL;
  exact_gaussian *input=NULL, *output=NULL;
  composition_trace trace;
  int result=2;
  if (composition_source_prepare(500,&plan)!=EXACT_CAPACITY || plan
      || composition_source_prepare(65536,&plan)!=EXACT_OK
      || numerical_values(&input,RECORDS,CAPACITY) || numerical_values(&output,RECORDS,CAPACITY)) goto done;
  for (unsigned example=0;example<5;++example) {
    for (unsigned index=0;index<RECORDS;++index)
      if (fixture(&input[index],example,index)) goto done;
    for (int opposite=0;opposite<2;++opposite) {
      observed_stages observed={0};
      exact_status status=composition_source_transform(plan,input,RECORDS,output,RECORDS,opposite,&trace,observe,&observed);
      if (status!=EXACT_OK || observed.stages!=4 || trace.stages!=8 || trace.line_calls!=24
          || !trace.motif_calls || trace.short_calls!=16 || trace.scalar_products<=512
          || !trace.prepared_matrix_entries || !trace.allocated_limb_bytes || !trace.allocated_descriptor_bytes) {
        fprintf(stderr,"source fixture %u/%d returned %u with %u stages and %" PRIu64 " lines\n",
                example,opposite,status,observed.stages,trace.line_calls);
        goto done;
      }
      printf("trace\t%u\t%d\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
             example,opposite,trace.stages,trace.line_calls,trace.motif_calls,trace.short_calls,trace.scalar_products);
      if (emit(example,opposite,output)) goto done;
    }
  }
  for (unsigned index=0;index<RECORDS;++index)
    if (exact_set(&output[index].real,1,3) || exact_set(&output[index].imaginary,-1,3)) goto done;
  observed_stages refused={.refuse=1};
  if (composition_source_transform(plan,input,RECORDS,output,RECORDS,2,&trace,NULL,NULL)!=EXACT_INVALID
      || composition_source_transform(plan,input,RECORDS,input,RECORDS,0,&trace,NULL,NULL)!=EXACT_INVALID
      || exact_set(&input[0].real,3,2)
      || composition_source_transform(plan,input,RECORDS,output,RECORDS,0,&trace,NULL,NULL)!=EXACT_INVALID
      || fixture(&input[0],4,0)
      || exact_set(&input[31].imaginary,1,8)
      || composition_source_transform(plan,input,RECORDS,output,RECORDS,0,&trace,NULL,NULL)!=EXACT_INVALID
      || fixture(&input[31],4,31)
      || composition_source_transform(plan,input,RECORDS,output,RECORDS,1,&trace,observe,&refused)!=EXACT_INVALID
      || refused.stages!=4) goto done;
  for (unsigned index=0;index<RECORDS;++index) {
    const exact_dyadic *real=&output[index].real, *imaginary=&output[index].imaginary;
    if (real->length!=1 || real->limbs[0]!=1 || real->sign!=1 || real->binary_scale!=3
        || imaginary->length!=1 || imaginary->limbs[0]!=1 || imaginary->sign!=-1 || imaginary->binary_scale!=3) goto done;
  }
  puts("refusals\tpass");
  result=ferror(stdout) ? 2 : 0;
done:
  numerical_clear_values(input,RECORDS);
  numerical_clear_values(output,RECORDS);
  composition_source_clear(plan);
  return result;
}
