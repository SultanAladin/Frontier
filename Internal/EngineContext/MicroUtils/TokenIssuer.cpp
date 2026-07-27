/*==============================================================================================================================================
                                                                TOKENISSUER.CPP
==============================================================================================================================================*/
// 🧩 Canonical slot mint/revoke path. IssueToken/RevokeToken/GrowOneSlot are store-parameterized templates, so every definition lives inline in
//    the header; this translation unit exists to satisfy the .{h,cpp} pairing and to compile the header standalone (catching regressions).

#include "EngineContext/MicroUtils/TokenIssuer.h"

// 📝 No out-of-line definitions: a template's members must be visible at every instantiation site, so they stay in the header.
//    Including the header here compiles it in isolation, which surfaces any missing include or syntax error the header would
//    otherwise hide until a downstream consumer first instantiates it.
