<!--PARSE:EMOJI-RULES v1.1
# Machine-readable emoji index. Stable keys; one rule per line: KEY = VALUE.
emoji.core.allowed       = 🧩,📝,💡,⚠️,🔴,🐞,🐛,🚧,🔍,🚩,✔️
emoji.symbol.allowed     = ⚙️,🧱,🔢,🔗,📐,⏱️,💾,🧵
emoji.dots.allowed       = 🔴,🟢
emoji.flags.allowed      = 🚩,🏳️,🏴,🏁,🎌
emoji.medals.allowed     = 🏆,🥇,🥈,🥉,🏅,🎖️
emoji.star.allowed       = ⭐
emoji.star.forbidden     = 🌟,✨
emoji.tag.allowed        = 🏷️
emoji.forbidden          = 🌟,✨
marker.module            = 🧩
marker.note              = 📝
marker.insight           = 💡
marker.warning           = ⚠️
marker.critical          = 🔴
marker.ok                = 🟢
marker.bug               = 🐞,🐛
marker.breaking          = 🚩
marker.tag               = 🏷️
rating.low               = ✔️
rating.medium            = 🚩
rating.high              = 🔴
rating.star              = ⭐
applies_to               = .h,.cpp,.slang,.wgsl,.vert,.frag,.comp,.glsl,.js,.md
PARSE:END-->

# SKILL-Emoji — Closed Emoji Specification

Single authority on which emojis may appear anywhere in Frontier — source comments, shader comments,
Markdown docs, chat, plans, decision tables, and commit messages.

🔴 **Only the emojis listed in this document may be used. Never introduce any emoji outside this table.**

---

## 1. Closed Whitelist

| Category     | Emoji             | Meaning                                   | Permitted locations                     |
|--------------|-------------------|-------------------------------------------|-----------------------------------------|
| Module       | 🧩                | File-header / docblock module description | Line 2 of file header; first `///` line |
| Notes        | 📝                | Implementation note                       | Comment above code, Markdown            |
| Notes        | 💡                | Design insight / non-obvious rationale    | Comment, Markdown                       |
| Notes        | ⚠️                | Caution — fragile assumption or ordering  | Comment, Markdown, chat                 |
| Notes        | 🔴                | Critical / mandatory / blocking           | Comment, Markdown, chat, tables         |
| Notes        | 🐞 / 🐛           | Known defect marker                       | Comment, Markdown                       |
| Notes        | 🚧                | Work in progress / incomplete             | Comment, Markdown                       |
| Notes        | 🔍                | Inspection / debug-only path              | Comment, Markdown                       |
| Symbol kind  | ⚙️                | Function / routine                        | Markdown & banners only, never inline   |
| Symbol kind  | 🧱                | Type — class, struct, union               | Markdown & banners only, never inline   |
| Symbol kind  | 🔢                | Constant or enumeration                   | Markdown & banners only, never inline   |
| Symbol kind  | 🔗                | Interop edge / vendor boundary            | Markdown & banners only, never inline   |
| Concern      | 📐                | Mathematical derivation                   | Comment, Markdown                       |
| Concern      | ⏱️                | Timing / performance concern              | Comment, Markdown                       |
| Concern      | 💾                | Memory or allocation concern              | Comment, Markdown                       |
| Concern      | 🧵                | Threading / concurrency concern           | Comment, Markdown                       |
| Status dots  | 🟢 / 🔴           | Good·passing·on / bad·failing·off         | Status lines, tables                    |
| Cost / scale | ✔️ / 🚩 / 🔴      | Low / medium / high                       | Decision tables, `/// cost`             |
| Quality star | ⭐                | Plain quality star (more = better)        | Decision tables, Markdown               |
| Medals       | 🏆 🥇 🥈 🥉 🏅 🎖️ | Rank / winner                             | Tables, Markdown, chat                  |
| Flags & tag  | 🏷️ 🚩 🏳️ 🏴 🏁 🎌 | Tag, priority, neutral, blocked, finished | Tables, Markdown, chat                  |

---

## 2. Hard Invariants

- **Status dots:** Only `🟢` and `🔴` exist. Amber, yellow, blue, and all other dot colours are forbidden.
- **Plain star only:** Use `⭐` exclusively. Fancy/sparkle stars `🌟` and `✨` are strictly forbidden everywhere.
- **Legend direction:** Every decision or cost table states scale direction in its legend
  (e.g., `Cost/Effort: ✔️ low · 🚩 medium · 🔴 high; Accuracy: 🔴 = best`).
- **Never use emoji as machine parse keys:** Machine keys use lowercase ASCII; normalize `✔️` with U+FE0F on write.
