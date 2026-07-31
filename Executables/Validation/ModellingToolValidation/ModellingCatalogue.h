/*==============================================================================================================================================
                                                         MODELLINGCATALOGUE.H
==============================================================================================================================================*/
// 🧩 The modelling workspace's tool catalogue and its selection strata. The card components in Internal/ render whatever tables they are
//    handed; this is the modelling workspace's set. A CAD or texture-paint card supplies its own against the same vocabulary.

#pragma once
#ifndef FRONTIER_VALIDATION_MODELLINGTOOL_MODELLINGCATALOGUE_H
#define FRONTIER_VALIDATION_MODELLINGTOOL_MODELLINGCATALOGUE_H

#include "EngineContext/Interface/Components/Menus/ToolCard/ToolCardSpecification.h"

namespace Frontier
{

//----------------------------------------------------------------------------------------------------------------------
//                                                          CONSTANTS
//----------------------------------------------------------------------------------------------------------------------

// 📝 One bit per selection stratum. None is a REAL stratum, not the absence of one: the creation tools (Primitive, Reference, Scene)
//    apply precisely when nothing is selected, so it needs a bit like any other.
constexpr unsigned int ToolStratumNone = 1u << 0;
constexpr unsigned int ToolStratumVertex = 1u << 1;
constexpr unsigned int ToolStratumEdge = 1u << 2;
constexpr unsigned int ToolStratumFace = 1u << 3;
constexpr unsigned int ToolStratumLoop = 1u << 4;
constexpr unsigned int ToolStratumBorder = 1u << 5;
constexpr unsigned int ToolStratumObject = 1u << 6;

// 🔴 The wildcard EXCLUDES None. A tool authored "*" transforms whatever is selected, and with nothing selected there is nothing to
//    transform — including None here would put Delete and Dissolve in the rail against an empty selection.
constexpr unsigned int ToolStrataAny = ToolStratumVertex | ToolStratumEdge | ToolStratumFace | ToolStratumLoop | ToolStratumBorder | ToolStratumObject;

constexpr int ModellingStratumCount = 7;

// The strata in rail order, for a host cycling through them.
constexpr unsigned int ModellingStrata[7] =
{
    ToolStratumNone,
    ToolStratumVertex,
    ToolStratumEdge,
    ToolStratumFace,
    ToolStratumLoop,
    ToolStratumBorder,
    ToolStratumObject,
};

//----------------------------------------------------------------------------------------------------------------------
//                                                        PUBLIC FUNCTIONS
//----------------------------------------------------------------------------------------------------------------------

// The band table and its length.
const ToolBandDescriptor* ResolveModellingBands(int& BandCount);

// The measurement readout authored for a stratum, or null when it has none.
const ToolProbeReadoutDescriptor* ResolveModellingProbe(unsigned int StratumBit);

// A stratum's display word, for the header badge and the footer tally.
const char* DescribeModellingStratum(unsigned int StratumBit);

// The prototype's authored selection size for a stratum.
int ResolveModellingSelectedCount(unsigned int StratumBit);

} // namespace Frontier

#endif
