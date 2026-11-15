# Compact T24 patch series

The normal upstream build and test targets remain available. T16 native code
generation stays supported. Before the native suffix, upstream's existing
`CodeGen::isSupported()` gate continues to reject T24.

Build and test with the normal targets:

```sh
cmake -S . -B build/t24 -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DLUAU_VECTOR_FOUR_WIDE=ON -DLUAU_T24_CANDIDATE=ON
cmake --build build/t24 --target Luau.Repl.CLI Luau.Conformance Luau.UnitTest
build/t24/Luau.Conformance --test-suite=Conformance
```

Interpreter regressions extend the existing conformance fixtures and use its
standard test main. Existing compiler/backend tests and CLI targets retain
their normal dependencies. No backend build switch is introduced.

The compact representation changes three upstream CodeGen references from
`offsetof(TValue, extra)` to the VM-owned `LUAU_TVALUE_EXTRA_OFFSET` in the same
commit that removes that member. These are compile compatibility edits; the
noncompact expression is unchanged and they do not enable native T24 code.
Native IR, lowering, support activation and optimizations follow the complete
interpreter series.

The compact profile requires 64-bit pointers and four inline float lanes.
Values are 24 bytes, nodes 40 bytes and table headers 48 bytes. An optional owned
plane adds four bytes per hash slot under the retained lazy admission policy.
The default reset limit is 64 usable slots. Keep VM, compiler, CodeGen and all
private-header consumers on the same profile; `Luau.Common` propagates the ABI.

The interpreter changes are ordered as safety fixes, constant hints, query
hashes, reset policy, compact representation, metadata ownership/iteration,
insertion and allocation refinements, copying, selection and string reads.
Representation and callback/OOM ownership transactions remain coherent commits.
Rebase those before replaying the native suffix.

## Native suffix

After the layout, semantic-vector IR, producer and separate-tag correctness
layers, T24 native execution is enabled only after validating its compact
bitfield encoding. Later optimizations remain separate commits.

```sh
build/t24/Luau.Conformance --test-suite=Conformance --codegen
build/t24/luau --codegen program.luau
```

The full profile has 42 definitions. The profile does not force global runtime
flags or adaptive compilation. The measured static policy uses ColdFunctions,
runtimeFeedback=false and no per-VM runtime recompilation.
