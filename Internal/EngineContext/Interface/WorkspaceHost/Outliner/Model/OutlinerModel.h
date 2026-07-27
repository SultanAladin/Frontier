/*==============================================================================================================================================
                                                              OUTLINERMODEL.H
==============================================================================================================================================*/
// 🧩 The flat row list the outliner renders. A workspace fills this from the Scene assembly each cycle (or on change) — the outliner reads it
//    and owns nothing. Keeping the list here (not inside the panel) means the SAME OutlinerPanel renders any workspace's tree: modeling
//    objects, UV islands, paint layers, sketch records — each workspace just builds its own rows.

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_OUTLINER_OUTLINERMODEL_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACEHOST_OUTLINER_OUTLINERMODEL_H

#include "OutlinerRow.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A bounded row buffer. Fixed capacity keeps the outliner allocation-free per cycle; a workspace with more records streams a window.
struct OutlinerModel
{
    static const int Capacity = 512;        // [-] - Max simultaneously-listed rows
    OutlinerRow      Rows[Capacity];        // [-] - Presented rows, tree pre-flattened by depth
    int              RowCount;              // [-] - Live row count
};


//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Clear the model back to empty (a workspace rebuilds rows before/at record time).
void ResetOutlinerModel(OutlinerModel& Model);

// 📝 Append one row. Silently ignored past Capacity. Returns the row's index, or -1 if full.
int AppendOutlinerRow(OutlinerModel& Model, const OutlinerRow& Row);

}   // namespace Frontier

#endif
