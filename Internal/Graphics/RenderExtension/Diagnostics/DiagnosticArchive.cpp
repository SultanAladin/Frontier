/*==============================================================================================================================================
                                                            DIAGNOSTICARCHIVE.CPP
==============================================================================================================================================*/
// 🧩 Implementation of the diagnostic log. Formats one classified entry as "[severity][tag] message  (file:line)" and routes it: faults and
//    cautions to stderr so they survive redirection, trace and notice to stdout. The shipping profile never reaches this file for trace/notice
//    (the macros compiled them out), so the only guard here is the always-live caution/fault path plus development-profile trace/notice.

#define _CRT_SECURE_NO_WARNINGS
#include "Graphics/RenderExtension/Diagnostics/DiagnosticArchive.h"

#include <cstdarg>
#include <cstdio>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                        INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// Short, fixed-width severity tag so aligned log lines read as columns.
const char* ResolveClassificationTag(DiagnosticClassification Classification)
{
    switch (Classification)
    {
        case DiagnosticClassification::Trace:   return "TRACE  ";
        case DiagnosticClassification::Notice:  return "NOTICE ";
        case DiagnosticClassification::Caution: return "CAUTION";
        case DiagnosticClassification::Fault:   return "FAULT  ";
    }
    return "?????? ";
}

// Strip the directory prefix so the location reads as "DiagnosticArchive.cpp:42", not the full absolute path.
const char* ResolveShortFile(const char* SourceFile)
{
    const char* ShortName = SourceFile;
    for (const char* Cursor = SourceFile; *Cursor != '\0'; ++Cursor)
    {
        if (*Cursor == '/' || *Cursor == '\\')
            ShortName = Cursor + 1;
    }
    return ShortName;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                         PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void BroadcastDiagnostic(DiagnosticClassification Classification,
                         const char*              SourceTag,
                         const char*              SourceFile,
                         int                      SourceLine,
                         const char*              MessageText,
                         ...)
{
    // 📝 Faults and cautions go to stderr (they must survive stdout redirection); trace/notice go to stdout.
    FILE* Destination = (Classification >= DiagnosticClassification::Caution) ? stderr : stdout;

    char FormattedBody[1024];
    va_list Arguments;
    va_start(Arguments, MessageText);
    vsnprintf(FormattedBody, sizeof(FormattedBody), MessageText, Arguments);
    va_end(Arguments);

    fprintf(Destination, "[%s][%s] %s  (%s:%d)\n",
            ResolveClassificationTag(Classification),
            SourceTag,
            FormattedBody,
            ResolveShortFile(SourceFile),
            SourceLine);
    fflush(Destination);
}

} // namespace Frontier
