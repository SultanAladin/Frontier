# SKILL-Formatting — Visual Formatting, Annotation & Math Rules

Binding on all C, C++, shader (`.slang`, `.wgsl`, `.glsl`), and engine Markdown output. Check every file
against this specification before returning it. Naming is governed by `SKILL-Naming.md`; emoji by `SKILL-Emoji.md`.

---

## 1. File Headers — Required on Every Source File

- Ruler above and below the title: `//===…`, **exactly 142 characters**.
- Title line: filename in ALL CAPS, strictly centred between the rulers.
- Immediately below: `// 🧩 [single-line module description]`, then **1 blank line**.

```cpp
//============================================================================================================================================
//                                                          BOUNDARYSTRUCTURE.H
//============================================================================================================================================
// 🧩 Radial-edge topology — generational tokens over vertex, edge, loop, and face records.
```

## 2. Section Banners

- Ruler above and below: `//---…`, **exactly 122 characters** (never below 80).
- Title in ALL CAPS, strictly centred. Exactly **1 blank line** before and after the banner.
- Name the *responsibility*, not the language construct (`TOPOLOGY QUERIES`, `MEMORY PROVISIONING`).

```cpp
//------------------------------------------------------------------------------------------------------------------------
//                                                     TOPOLOGY QUERIES
//------------------------------------------------------------------------------------------------------------------------
```

## 3. Comments & Symbol Annotations

- **Inline comments** align vertically as a column with a bracketed unit tag first:
  `Type Name;   // [unit] - description` (`[m]`, `[mm]`, `[px]`, `[ms]`, `[Hz]`, `[rad]`, `[deg]`, `[B]`, `[-]`).
- **Implementation notes** sit above the code they explain: `// 📝 …`.
- **Ordered steps** use `①–⑩`, one step per line, aligned.
- **No commented-out code.** Delete dead code; Git preserves history.
- **Symbol-index docblocks (`///`)** declare `in`, `out`, `err`, `cost`, and `tag` without hand-written line numbers:

```cpp
/// 🧩 Resolves the incident faces around an active topological edge.
/// in    ActiveEdge  BoundaryToken<EdgeTag>  [-]  generational edge token
/// out   -           FaceSpan                [-]  contiguous span of incident faces
/// err   returns an empty span for a vacant or stale token; never throws
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
FaceSpan ResolveIncidentFaces(BoundaryToken<EdgeTag> ActiveEdge) const;
```

## 4. Mathematical & Physical Variables

- **MANDATORY:** Use real Greek and Unicode mathematical symbols — `β`, `γ`, `Δτ`, `𝑣`, `𝑐`, `θ`, `ω`, `ε`, `‖·‖` —
  never ASCII transliterations (`beta`, `dTau`, `epsilon`).
- This is the **only** sanctioned exception to PascalCase identifiers.

## 5. Alignment, Braces & Indentation

- **Assignments:** align `=` vertically within a logical block; reset on a new logical block.
- **Struct fields:** align type, identifier, and `// [unit]` comment columns using spaces.
- **Parameters:** 4 or more parameters go one per line, types and names vertically aligned.
- **Braces:** **Allman** style — opening `{` on its own line (single-line guards may omit braces: `if (Count == 0) return;`).
- **Indentation:** **4 spaces** per level. **STRICTLY NO TABS.**
- **Namespaces:** contents stay **FLAT** — never indent inside a `namespace` block.
- **Blank lines:** 1 between functions, 2 between major section groups, at most 1 inside a function body, zero trailing whitespace, single trailing newline.

## 6. Markdown

- `#` is reserved for the document title only; use `##` / `###` for sections.
- Maximum line width **120 characters**. Bullets use `-`. Markdown tables align `|` columns with spaces.
- Fenced code blocks always declare a language tag (`cpp`, `slang`, `powershell`, `python`, `sh`, `text`).
- Emoji usage defers strictly to `SKILL-Emoji.md`.
