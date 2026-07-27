/*==============================================================================================================================================
                                                                RECORDENTRY.CPP
==============================================================================================================================================*/
// 🧩 One item in the scene directory. The footprint is a trivial value type and its presentation operators are constexpr inline, so every
//    definition lives in the header; this translation unit satisfies the .{h,cpp} pairing and compiles the header standalone to catch regressions.

#include "EngineContext/Scene/RecordEntry.h"

// 📝 No out-of-line definitions: RecordEntry is a plain value footprint and its operators are constexpr. Including the header
//    here compiles it in isolation, surfacing any missing include the header would otherwise hide until first use downstream.
