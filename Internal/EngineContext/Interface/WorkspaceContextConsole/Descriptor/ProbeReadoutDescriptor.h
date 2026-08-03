/*==============================================================================================================================================
                                                          PROBEREADOUTDESCRIPTOR.H
==============================================================================================================================================*/
// 🧩 The readout the console's second-slide left column shows: what the current context measures, as titled sections of rows. Ported unchanged in
//    spirit from ToolCard's ToolProbeReadoutDescriptor — the probe never carried a gate, so it needed no subtraction. A workspace fills this per
//    frame with whatever its selection currently measures; a workspace with nothing to report supplies null and the console draws no probe.
//
//    📝 Four row shapes share one struct, distinguished by which members are set: a scalar (Key/Reading/Unit), a vector (VectorLabel + Axis*), a
//       condition (ConditionLabel + ConditionMet), and a bounded scalar that colours its reading when it leaves [LimitFloor, LimitCeiling].

#pragma once
#ifndef FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_PROBEREADOUTDESCRIPTOR_H
#define FRONTIER_ENGINECONTEXT_INTERFACE_WORKSPACECONTEXTCONSOLE_DESCRIPTOR_PROBEREADOUTDESCRIPTOR_H

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                            CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int ProbeAxisLimit      = 3;    // [idx] - a vector readout is XYZ
constexpr int ProbeRowLimit       = 6;    // [idx] - rows in one section (peak 4)
constexpr int ProbeSectionLimit   = 5;    // [idx] - sections in one readout (peak 4)
constexpr int ProbeAggregateLimit = 6;    // [idx] - aggregate rows under a multi-selection (peak 5)


//------------------------------------------------------------------------------------------------------------------------
//                                                            STRUCTS
//------------------------------------------------------------------------------------------------------------------------

struct ProbeRowDescriptor
{
    const char* Key;                                // [-] - scalar row caption
    const char* Reading;                            // [-] - scalar row value, pre-formatted
    const char* Unit;                               // [-] - scalar row unit

    const char* VectorLabel;                        // [-] - vector row caption; non-null selects the vector shape
    const char* AxisLabels[ProbeAxisLimit];         // [-] - "X" "Y" "Z"
    const char* AxisReadings[ProbeAxisLimit];       // [-] - per-axis values, pre-formatted
    const char* VectorUnit;                         // [-] - unit shared by the axes

    const char* ConditionLabel;                     // [-] - condition row caption; non-null selects the condition shape
    bool        ConditionMet;                       // [-] - drives the dot colour

    bool        LimitPresent;                       // [-] - true when the bounds below apply
    float       LimitFloor;                         // [-] - reading below this colours as a breach
    float       LimitCeiling;                       // [-] - reading above this colours as a breach
};


struct ProbeSectionDescriptor
{
    const char*        Title;                        // [-]  - uppercase section rule
    ProbeRowDescriptor Rows[ProbeRowLimit];          // [-]  - rows in order
    int                RowCount;                     // [idx]- how many are set
};


struct ProbeReadoutDescriptor
{
    const char*            Identity;                            // [-]  - what is being measured
    ProbeSectionDescriptor Sections[ProbeSectionLimit];         // [-]  - single-selection readout
    int                    SectionCount;                        // [idx]- how many are set
    ProbeRowDescriptor     Aggregate[ProbeAggregateLimit];      // [-]  - multi-selection readout
    int                    AggregateCount;                      // [idx]- how many are set
};

} // namespace Frontier

#endif
