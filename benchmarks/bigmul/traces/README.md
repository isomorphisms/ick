# Executed trace format v1

The TSV trace binds source, binary, exact ordered input-limb case, profile,
completeness and coordinate space. Events have sequential IDs, explicit units,
widths and references to prior events. Values describe executed schoolbook
reads, writes, multiply-accumulates, carries, results and buffer lifetimes.
Coordinates are logical limb indices. They do not describe observed addresses,
cache misses or an FFT, and the source does not introduce a compiler IR.

The control is bounded to 16 limbs per operand, so complete output is bounded.
Every 64-bit intermediate, 32-bit store/carry, input, output and operation count
is validated independently. Trace-enabled output is compared to the existing
uninstrumented C harness and Python integers. Output/formatting failure returns
nonzero. Dropped events, incomplete traces and mismatched executable identities
are refused. A trace identity is a binding receipt, not proof of binary semantics.

`check_mb17.py` emits raw traces and separate process-endpoint timings for the
traced and plain controls. Startup and formatting enter both distributions;
the difference is not claimed as pure observer overhead or multiplication time.
## Retained recursive control

`check_recursive.pi` executes the existing MB05 Karatsuba provider through the
checked Ithon frontend. Its separate `recursive-trace-v1` format leaves the
schoolbook format unchanged. Compile-time hooks record real call entry/return,
parent-child relations, padded operand and subproduct snapshots, difference
buffer movement, scratch allocation/free and schoolbook provider calls. These
are logical call and limb coordinates. They do not measure physical addresses,
cache misses, all primitive operations inside the opaque schoolbook call, or a
paper transform. Native builds without `MB17_TRACE` preprocess the hooks away.

The observer accepts at most 64 stored limbs per operand and 128 events. The
validator independently checks every returned subproduct using exact integers,
the low/high/absolute-difference child operands, exact tree size, call ordering,
scratch lifetimes and final output words. Four fixtures cover a leaf, one and
two recursive levels, maximal limbs, unequal widths and retained high zeroes.
Identity/completeness, units, dropped/duplicate events, missing frees, wrong
parents and altered returned subproducts have targeted refusals. Source and
binary hashes bind the run; the source manifest additionally binds both trace
and provider headers, the linked harness, and its shared guard header when
present. Metadata is binding provenance, not independent proof of semantics.

The verifier prints a separate six-trial alternating-order endpoint experiment.
Those raw integer nanoseconds include startup, formatting and collection; their
difference is not pure arithmetic observer overhead. Set
`MB17_RECURSIVE_RAW` to an absolute output directory to retain all four complete
traces plus `sources.tsv` beside the printed receipt. The native workflow uses
the explicitly pinned Ithon frontend; ordinary Python is not an execution path
for this new verifier.

Paper traces still wait for MB16. Transform, precision-change and paper-provider
events remain unexecuted future scope and cannot be renamed from these controls.
