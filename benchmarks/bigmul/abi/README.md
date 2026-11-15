# MB-02 checked direct calls

`checked_product.h` is the shared, header-only correctness adapter used by the
hex protocol and available to MB-05, MB-11, MB-14 and MB-16. It calls a supplied
function with the existing ABI:

```c
int multiply(const uint32_t *a, size_t na,
             const uint32_t *b, size_t nb, uint32_t *out);
```

For `mb02_checked_product`, supply that callback, each input storage pointer,
positive stored limb length and declared capacity, followed by the output
storage pointer and capacity. Capacities count `uint32_t` limbs. All limbs are
least-significant-first, independent of host byte order; stored zero has one
zero limb, and leading zero limbs are permitted. Every successful call writes
all `na + nb` output limbs.

The adapter refuses missing pointers/callbacks, zero lengths, insufficient
declared capacities, overflowing sizes/address intervals, misalignment and
overlap between active output and either active input before calling the
provider. Read-only inputs may overlap, including a square. The checked
boundary takes `void` storage pointers so it can refuse misalignment before
converting them to typed pointers. Address-interval checks use `uintptr_t` on
the supported flat-address targets. Caller-declared capacities cannot prove
the actual sizes or lifetimes of objects; valid backing storage remains a
caller obligation.

Two guarded calls with distinct output prefills check full output stores,
adjacent guard words, preserved input snapshots and provider status. Failed
calls leave the caller's output untouched. A faulty provider may already have
mutated an input; the adapter detects that failure without promising rollback.
Arbitrary memory corruption requires sanitizer execution, and a consistent
wrong answer requires an independent arithmetic oracle. Keep this adapter
outside arithmetic-only timing intervals. The timed benchmark still calls
the original kernel directly.

`direct_call_guard.c` is a reusable C fixture linked against the existing
schoolbook kernel. Its ordinary invocation checks nineteen admission refusals
on normally aligned hosts, a maximum-carry aliased square, leading zeroes,
zero, full writes and unchanged caller tail storage. Its named defective
providers must return nonzero from the outer process: before/after-output
writes, altered input, omitted upper zero, no writes, failing status, and a
consistent wrong value. The last case demonstrates the separate oracle
boundary. The inherited `check_mb02.py` compiles and executes this fixture.

The header preserves the original four guard allocations, so the protocol's
eight-site allocation-failure controls retain their meaning. The original
ABI, timed path and kernel implementation are unchanged. Host, sanitizer,
32-bit, qualified ICK and physical-device results remain separate evidence.
