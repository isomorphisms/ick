#include "composition.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { RECORDS=32, PRECISION=256, WORD_BYTES=33, CAPACITY=9 };

static exact_status fixture(exact_gaussian *value,unsigned test,unsigned index) {
  int64_t real=0,imaginary=0;
  switch (test) {
  case 1: real=index==0 ? 8 : 0; break;
  case 2: imaginary=index==7 ? -8 : 0; break;
  case 3: real=index==31 ? 7 : 0; imaginary=index==31 ? 10 : 0; break;
  case 4: real=(int64_t)(index%7)-3; imaginary=(int64_t)((index*5)%11)-5; break;
  case 5: real=index==0 ? 16 : 0; break;
  }
  exact_status status=exact_set(&value->real,real,4);
  return status==EXACT_OK ? exact_set(&value->imaginary,imaginary,4) : status;
}

static exact_status emit(unsigned example,const exact_gaussian *values,size_t length) {
  for (size_t index=0;index<length;++index) {
    unsigned char real[WORD_BYTES],imaginary[WORD_BYTES];
    if (exact_serialize(&values[index].real,PRECISION,real,WORD_BYTES)
        || exact_serialize(&values[index].imaginary,PRECISION,imaginary,WORD_BYTES)) return EXACT_INVALID;
    printf("value\t%u\t%zu\t",example,index);
    for (size_t byte=0;byte<WORD_BYTES;++byte) printf("%02x",real[byte]);
    putchar('\t');
    for (size_t byte=0;byte<WORD_BYTES;++byte) printf("%02x",imaginary[byte]);
    putchar('\n');
  }
  return ferror(stdout) ? EXACT_INVALID : EXACT_OK;
}

typedef struct { unsigned stages, refuse_last; } stages_seen;
static exact_status observe(void *opaque,const char *stage,const exact_gaussian *values,size_t length) {
  static const char *const names[]={"chirp-input-phase","chirp-half-twist","chirp-ring-convolution",
                                    "chirp-untwist-restore","chirp-output-phase"};
  stages_seen *seen=opaque;
  if (!values || length!=RECORDS || seen->stages>=sizeof(names)/sizeof(names[0])
      || strcmp(names[seen->stages],stage)) return EXACT_INVALID;
  ++seen->stages;
  return seen->refuse_last && seen->stages==5 ? EXACT_INVALID : EXACT_OK;
}

int main(void) {
  composition_chirp_plan *plan=NULL;
  exact_gaussian input[RECORDS]={0},output[RECORDS]={0};
  composition_trace trace;
  int result=2;
  if (composition_chirp_prepare(100,&plan)!=EXACT_CAPACITY || plan
      || composition_chirp_prepare(65536,&plan)!=EXACT_OK) goto done;
  for (unsigned index=0;index<RECORDS;++index) {
    if (exact_init(&input[index].real,CAPACITY) || exact_init(&input[index].imaginary,CAPACITY)
        || exact_init(&output[index].real,CAPACITY) || exact_init(&output[index].imaginary,CAPACITY)) goto done;
  }
  for (unsigned example=0;example<6;++example) {
    for (unsigned index=0;index<RECORDS;++index)
      if (fixture(&input[index],example,index)) goto done;
    stages_seen seen={0};
    exact_status status=composition_chirp(plan,input,RECORDS,output,RECORDS,&trace,observe,&seen);
    if (status!=EXACT_OK || seen.stages!=5 || trace.stages!=5 || !trace.motif_calls
        || trace.short_calls!=16 || trace.scalar_products!=4*4*RECORDS
        || !trace.prepared_matrix_entries || !trace.allocated_limb_bytes || !trace.allocated_descriptor_bytes) {
      fprintf(stderr,"chirp fixture %u returned %u with %u stages\n",example,status,seen.stages);
      goto done;
    }
    printf("trace\t%u\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\n",
           example,trace.stages,trace.motif_calls,trace.short_calls,trace.scalar_products);
    if (emit(example,output,RECORDS)) goto done;
  }

  for (unsigned index=0;index<RECORDS;++index) {
    if (exact_set(&output[index].real,1,3) || exact_set(&output[index].imaginary,-1,3)) goto done;
  }
  stages_seen refused={.refuse_last=1};
  if (composition_chirp(plan,input,RECORDS-1,output,RECORDS,&trace,NULL,NULL)!=EXACT_INVALID
      || composition_chirp(plan,input,RECORDS,input,RECORDS,&trace,NULL,NULL)!=EXACT_INVALID
      || exact_set(&input[31].real,3,1)
      || composition_chirp(plan,input,RECORDS,output,RECORDS,&trace,NULL,NULL)!=EXACT_INVALID
      || fixture(&input[31],5,31)
      || composition_chirp(plan,input,RECORDS,output,RECORDS,&trace,observe,&refused)!=EXACT_INVALID
      || refused.stages!=5) goto done;
  for (unsigned index=0;index<RECORDS;++index) {
    const exact_dyadic *real=&output[index].real,*imaginary=&output[index].imaginary;
    if (real->length!=1 || real->limbs[0]!=1 || real->sign!=1 || real->binary_scale!=3
        || imaginary->length!=1 || imaginary->limbs[0]!=1 || imaginary->sign!=-1 || imaginary->binary_scale!=3) goto done;
  }
  puts("refusals\tpass");
  result=ferror(stdout) ? 2 : 0;
done:
  for (unsigned index=0;index<RECORDS;++index) {
    exact_clear(&input[index].real);
    exact_clear(&input[index].imaginary);
    exact_clear(&output[index].real);
    exact_clear(&output[index].imaginary);
  }
  composition_chirp_clear(plan);
  return result;
}
