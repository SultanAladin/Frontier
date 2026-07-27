/*==============================================================================================================================================
                                                              PANELREGISTRY.CPP
==============================================================================================================================================*/
// 🧩 Fixed-capacity panel table. No allocation, no ownership — the workspace owns the panel context; the registry only records where + how to
//    place it. This is the seam that keeps the host generic over any workspace's panel set.

#include "PanelRegistry.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

void ResetPanelRegistry(PanelRegistry& Registry)
{
    Registry.EntryCount = 0;
}


void RegisterPanel(PanelRegistry& Registry, const PanelRegistration& Registration)
{
    if (Registry.EntryCount >= PanelRegistry::Capacity)
    {
        return;
    }
    Registry.Entries[Registry.EntryCount] = Registration;
    Registry.EntryCount += 1;
}


const PanelRegistration* ResolvePanelForRegion(const PanelRegistry& Registry, PanelDockRegion Region)
{
    for (int Index = 0; Index < Registry.EntryCount; ++Index)
    {
        if (Registry.Entries[Index].Region == Region)
        {
            return &Registry.Entries[Index];
        }
    }
    return nullptr;
}

}   // namespace Frontier
