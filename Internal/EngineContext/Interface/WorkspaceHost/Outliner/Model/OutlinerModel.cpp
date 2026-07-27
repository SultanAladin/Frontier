/*==============================================================================================================================================
                                                              OUTLINERMODEL.CPP
==============================================================================================================================================*/
// 🧩 Fixed-capacity row buffer. No allocation, no scene ownership — just the flattened view a workspace hands the outliner each cycle.

#include "OutlinerModel.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ResetOutlinerModel(OutlinerModel& Model)
{
    Model.RowCount = 0;
}


int AppendOutlinerRow(OutlinerModel& Model, const OutlinerRow& Row)
{
    if (Model.RowCount >= OutlinerModel::Capacity)
    {
        return -1;
    }
    const int Index = Model.RowCount;
    Model.Rows[Index] = Row;
    Model.RowCount += 1;
    return Index;
}

}   // namespace Frontier
