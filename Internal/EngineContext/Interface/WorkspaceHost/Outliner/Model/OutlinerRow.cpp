/*==============================================================================================================================================
                                                              OUTLINERROW.CPP
==============================================================================================================================================*/
// 🧩 Record-identity helpers for outliner rows. Kept trivial + separate so token comparison lives in one place (a slot-index compare would be
//    the exact bug the generation stamp exists to prevent).

#include "OutlinerRow.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

bool OutlinerRecordTokensEqual(const OutlinerRecordToken& A, const OutlinerRecordToken& B)
{
    return A.Index == B.Index && A.Generation == B.Generation;
}


OutlinerRecordToken ResolveNullRecordToken()
{
    OutlinerRecordToken Null = {};
    Null.Index      = 0;
    Null.Generation = 0;
    return Null;
}

}   // namespace Frontier
