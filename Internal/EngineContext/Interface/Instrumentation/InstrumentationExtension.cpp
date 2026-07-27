/*==============================================================================================================================================
                                                          INSTRUMENTATIONEXTENSION.CPP
==============================================================================================================================================*/
// 🧩 The overlay lifecycle + per-paint spine: pull every signal, host each card, run the pill tray, apply close/re-open

#include "InstrumentationExtension.h"

#include "InstrumentProjection/InstrumentEnclosurePass.h"
#include "SpatialAlignment/InactiveInstrumentLinearizer.h"
#include "SignalAccumulation/SignalIntakeProfile.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Pull every bound signal of one instrument through its callback and append to the matching ring. A panel binds only the
    //    leading slots it reads (one for a readout, seven for the render report), so we walk exactly SignalCount — an unbound
    //    slot is never pulled and its ring stays flat.
    void AccumulateInstrument(InstrumentRecordEntry& Record)
    {
        for (uint32_t Signal = 0u; Signal < Record.SignalCount; ++Signal)
        {
            AppendSignalSample(Record.History[Signal], ResolveIngestedSample(Record.Ingestion[Signal]));
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Clear the store and arm the overlay. Nothing to allocate — the overlay is pure draw-list.
void InitializeInstrumentationExtension(InstrumentationExtension& Extension)
{
    ResetInstrumentRecordArchive(Extension.Store);
    Extension.ReadyStatus = true;
}

// Forward a registration to the store. The returned token is vacant when the store is full.
InstrumentToken RegisterInstrumentCard(InstrumentationExtension& Extension, const InstrumentRegistration& Registration)
{
    return RegisterInstrument(Extension.Store, Registration);
}

// Forward a withdrawal to the store.
void WithdrawInstrumentCard(InstrumentationExtension& Extension, InstrumentToken Token)
{
    WithdrawInstrument(Extension.Store, Token);
}

// The per-paint spine. Runs in three sweeps over the store: (1) pull + accumulate every live instrument; (2) host each
// expanded card and note any close click; (3) draw the pill tray for collapsed cards and note any re-open click. Close/
// re-open are applied after the walks so we never mutate a record's Collapsed flag mid-iteration.
void RefreshInstrumentationExtension(InstrumentationExtension& Extension, float ViewportHeight)
{
    if (!Extension.ReadyStatus)
    {
        return;
    }

    InstrumentRecordArchive& Store = Extension.Store;

    // 📝 (1) Pull every live instrument's signal into its ring — one callback per trace, no allocation.
    for (uint32_t Slot = 0u; Slot < Store.HighWaterMark; ++Slot)
    {
        InstrumentRecordEntry& Record = Store.Records[Slot];
        if (Record.Occupied)
        {
            AccumulateInstrument(Record);
        }
    }

    // 📝 (2) Host each expanded card. A close click is deferred into the record's Collapsed flag (safe — reading it, not the
    //    store structure). The enclosure borrows the shared scratch span, so hosting N cards allocates nothing.
    for (uint32_t Slot = 0u; Slot < Store.HighWaterMark; ++Slot)
    {
        InstrumentRecordEntry& Record = Store.Records[Slot];
        if (!Record.Occupied || Record.Collapsed)
        {
            continue;
        }

        InstrumentEnclosureOutcome Outcome = ConstructInstrumentEnclosure(Record, Slot,
                                                                          Extension.ScratchSpan,
                                                                          SignalRingCapacity * InstrumentSignalCapacity);
        if (Outcome.CloseRequested)
        {
            Record.Collapsed = true;   // 📝 Collapse into the pill tray; the ring keeps accumulating while dormant.
        }
    }

    // 📝 (3) The bottom-left pill tray for collapsed cards. A pill click re-opens (un-collapses) that slot.
    InactiveLinearizerOutcome TrayOutcome = ConstructInactiveInstrumentRow(Store, ViewportHeight);
    if (TrayOutcome.ReopenRequested && TrayOutcome.ReopenSlotIndex < InstrumentCapacity)
    {
        InstrumentRecordEntry& Record = Store.Records[TrayOutcome.ReopenSlotIndex];
        if (Record.Occupied)
        {
            Record.Collapsed = false;
        }
    }
}

// Clear the store. No device resources to release.
void FinalizeInstrumentationExtension(InstrumentationExtension& Extension)
{
    ResetInstrumentRecordArchive(Extension.Store);
    Extension.ReadyStatus = false;
}

}   // namespace Frontier
