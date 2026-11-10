# SKILL-SymbolIndex — Read the Index Before Source Files

A `.symbolindex` is a generated per-folder digest listing every symbol on one row: signature, file,
`first-last` line span, parameter units, return, cost, and callers.

- **Folder has `<Folder>/<Folder>.symbolindex`:** Read the index first, never full source files.
- **Body genuinely required:** Read **only** the `first-last` line span (`offset = first`, `limit = last - first + 1`).
  Never widen reads just for surrounding context.
- **Status markers:** `?` = undocumented work item; `-` = known empty; `|` separates columns.
- **Generated artefact:** Never hand-edit `.symbolindex` files; regenerate when stale before trusting line spans.

```text
F Normalize | VectorProjection.cpp | 118-143 | api,pure,simd | ✔️ | Scales 𝑣 to unit length
    in   𝑣  const Vector3&  [-]  direction of any magnitude, may be zero
    out  -  Vector3         [-]  unit vector, ‖r‖ = 1 ± ε
    err  returns (0,0,0) when ‖𝑣‖ < ε; never throws
```

Kinds: `F` function · `T` type · `E` enum · `V` constant · `A` alias · `K` macro · `X` interop · `S` source file · `I` child index.
Cost scale: `✔️` low → `🚩` medium → `🔴` high.
