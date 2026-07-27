/*==============================================================================================================================================
                                                          INSTRUMENTATIONVALIDATION.CPP
==============================================================================================================================================*/
// 🧩 Headless checks for the data layer: ring ordering/min-max, vacancy LIFO, store register/withdraw/resolve token staleness

#include "InstrumentationValidation.h"

#include "SignalAccumulation/MetricSignalAccumulator.h"
#include "Registry/InstrumentVacancyTable.h"
#include "Registry/InstrumentRecordArchive.h"

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                          INTERNAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 One assertion: bump the checked tally always, the passed tally only when the condition holds. Keeps each check a single
    //    readable line while the report accumulates.
    void Confirm(InstrumentationValidationReport& Report, bool Condition)
    {
        Report.CheckedCount = Report.CheckedCount + 1u;
        if (Condition)
        {
            Report.PassedCount = Report.PassedCount + 1u;
        }
    }

    // 📝 Ring accumulator: append a known ascending sequence, unroll, and confirm oldest→newest ordering plus min/max/latest.
    void ValidateAccumulator(InstrumentationValidationReport& Report)
    {
        MetricSignalAccumulator Accumulator;
        ResetSignalAccumulator(Accumulator);

        for (uint32_t Index = 0u; Index < 5u; ++Index)
        {
            AppendSignalSample(Accumulator, (float)Index);   // 📝 0,1,2,3,4
        }

        float Scratch[SignalRingCapacity];
        SignalEvaluationInterval Range = RetrieveEvaluationRange(Accumulator, Scratch, SignalRingCapacity);

        Confirm(Report, Range.Count == 5u);
        Confirm(Report, Range.Samples != nullptr && Range.Samples[0] == 0.0f);      // 📝 oldest first
        Confirm(Report, Range.Latest == 4.0f);                                      // 📝 newest last
        Confirm(Report, Range.Minimum == 0.0f && Range.Maximum == 4.0f);

        // 📝 Overflow the ring so it wraps, then confirm only the newest SignalRingCapacity samples survive, still ordered.
        for (uint32_t Index = 0u; Index < SignalRingCapacity + 10u; ++Index)
        {
            AppendSignalSample(Accumulator, (float)Index);
        }
        Range = RetrieveEvaluationRange(Accumulator, Scratch, SignalRingCapacity);
        Confirm(Report, Range.Count == SignalRingCapacity);
        Confirm(Report, Range.Latest == (float)(SignalRingCapacity + 10u - 1u));
    }

    // 📝 Vacancy table: reclaim three slots, confirm LIFO pop order, then confirm empty reports false.
    void ValidateVacancy(InstrumentationValidationReport& Report)
    {
        InstrumentVacancyTable Table;
        ResetVacancyTable(Table);

        ReclaimVacancy(Table, 7u);
        ReclaimVacancy(Table, 3u);
        ReclaimVacancy(Table, 9u);

        uint32_t Slot = 0u;
        Confirm(Report, AcquireVacancy(Table, Slot) && Slot == 9u);   // 📝 most-recently-freed first
        Confirm(Report, AcquireVacancy(Table, Slot) && Slot == 3u);
        Confirm(Report, AcquireVacancy(Table, Slot) && Slot == 7u);
        Confirm(Report, !AcquireVacancy(Table, Slot));               // 📝 empty now
    }

    // 📝 Store: register two instruments, resolve both, withdraw one, confirm the stale token no longer resolves and the slot
    //    recycles for the next registration (with a bumped generation).
    void ValidateStore(InstrumentationValidationReport& Report)
    {
        InstrumentRecordArchive Store;
        ResetInstrumentRecordArchive(Store);

        InstrumentRegistration First;
        First.Title = "First";
        InstrumentRegistration Second;
        Second.Title = "Second";

        InstrumentToken TokenA = RegisterInstrument(Store, First);
        InstrumentToken TokenB = RegisterInstrument(Store, Second);

        Confirm(Report, !VacantToken(TokenA) && !VacantToken(TokenB));
        Confirm(Report, Store.LiveCount == 2u);
        Confirm(Report, ResolveInstrument(Store, TokenA) != nullptr);
        Confirm(Report, ResolveInstrument(Store, TokenB) != nullptr);

        WithdrawInstrument(Store, TokenA);
        Confirm(Report, Store.LiveCount == 1u);
        Confirm(Report, ResolveInstrument(Store, TokenA) == nullptr);   // 📝 stale token no longer resolves

        // 📝 The next registration reuses TokenA's slot with a higher generation, so TokenA still fails to resolve.
        InstrumentRegistration Third;
        Third.Title = "Third";
        InstrumentToken TokenC = RegisterInstrument(Store, Third);
        Confirm(Report, TokenC.SlotIndex == TokenA.SlotIndex);
        Confirm(Report, TokenC.Generation != TokenA.Generation);
        Confirm(Report, ResolveInstrument(Store, TokenA) == nullptr);
        Confirm(Report, ResolveInstrument(Store, TokenC) != nullptr);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Run every data-layer check and return the accumulated tally.
InstrumentationValidationReport RunInstrumentationValidation()
{
    InstrumentationValidationReport Report;
    ValidateAccumulator(Report);
    ValidateVacancy(Report);
    ValidateStore(Report);
    return Report;
}

}   // namespace Frontier
