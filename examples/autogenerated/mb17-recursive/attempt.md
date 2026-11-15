# Executed recursive multiplication trace

## Values, types and actions

The existing MB05 Karatsuba control maps two bounded unsigned limb arrays to
their exact product. The recursive trace maps each actually entered call to a
unique call ID, parent ID, equal power-of-two input width, exact input/output
snapshots, difference-buffer movement, scratch allocation/free and opaque
schoolbook calls. These are logical call/limb coordinates, not physical memory
traffic. A successful complete trace must have a balanced call tree and scratch
lifetimes, three child subproducts for each non-leaf call, and exact independent
integer equality for every returned subproduct.

`check_call : Call → Either Diagnostic CheckedCall` is pure. Compiling C,
executing children, reading traces and recording raw endpoint durations are
effects. Endpoint durations include process startup and formatting; they are
not arithmetic timings or a paper-candidate performance result.

`RecursiveCount.idric` preserves the smallest domain attempt using the syntax
of the existing retained Idriç examples. No `idric` executable was found in this
consumer's host PATH (`command -v idric`, exit 1, 2026-11-15). The attempt is
NOT_RUN. It does not claim checked file/process/clock interfaces. Those services
are the first concrete consumer boundary for an Idriç port.

## Implementation boundary

The provider remains the existing C `providers/karatsuba.c`; compile-time hooks
observe its real recursion rather than reimplementing multiplication. Ordinary
builds preprocess the hooks away. The C driver bounds operands to 64 limbs and
the trace to 128 events. New validation/orchestration uses checked Ithon `.pi`,
through the actual frontend at `74ab6d6044b07b76a910978437a34a6b5ac6acda`.
The existing schoolbook trace-v1 and its downstream consumers remain unchanged.

The Ithon integer oracle checks each call product and the three actual child
operand pairs against the parent's halves and absolute differences. Targeted
trace defects exercise sequence, parent, product, lifetime, source identity,
unit and completeness refusals. Native ICK and physical-device execution are
separate acceptance tiers.

## Verification

Actual invocation on the Linux x86_64 host:

`/workspace/scratch/c27e98e48eb3/ithon/ithon /workspace/scratch/c27e98e48eb3/ick-mb17/benchmarks/bigmul/traces/check_recursive.pi`

The checked program passed four real C-provider cases, eight trace refusals and
the operand-budget refusal. The 64-limb fixture executed a 13-call tree with
nine schoolbook leaves. Six alternating-order endpoint trials retain raw
nanoseconds without attributing their full difference to arithmetic overhead.
The uninstrumented provider object, compiled before and after the hook changes
with host `cc -O2 -std=c11 -Wall -Wextra -Werror`, is byte-identical:
`0761965466869b109f8f24292ad82a30eaac6ae5f42a015ec750155fce20ced4`.

The first checked frontend attempt rejected a generator comprehension with
`cannot infer static type of GeneratorExp`. The implementation now uses typed
loops and explicit extraction of foreign byte/clock/path results. It executes
through Ithon, without a Python fallback. This describes the Ithon language
boundary; it does not claim an Idriç compiler rejection.

The required next Idriç step remains a verified consumer compiler entrypoint,
then checked bounded process/text input and the same refusal cases.
