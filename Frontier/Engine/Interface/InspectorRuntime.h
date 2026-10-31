/*==========================================================================================================================================
                                                         INSPECTORRUNTIME.H
==========================================================================================================================================*/
// 🧩 Central Editor runtime managing modular RecordInspector attachments, lifecycle slots, and *Editor dispatch.

#pragma once

#include "Engine/Diagnostics/BuildConfiguration.h"
#include "Engine/Scene/RecordToken.h"
#include "Engine/Scene/SceneDirectory.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR SWITCH ENTRY
//------------------------------------------------------------------------------------------------------------------------

struct InspectorSwitchEntry
{
    const char* Label   = "";
    const char* SubNote = "";
    bool*       Target  = nullptr;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    RECORD INSPECTOR BASE
//------------------------------------------------------------------------------------------------------------------------

class RecordInspector
{
  public:
    virtual ~RecordInspector() = default;

    [[nodiscard]] virtual const char* QueryEyebrowText() const noexcept                     = 0;
    [[nodiscard]] virtual const char* QuerySectionNote() const noexcept                     { return "Live"; }
    [[nodiscard]] virtual const char* QueryEditorButtonLabel() const noexcept               { return nullptr; }
    virtual void                      ConstructEditor(RecordToken Token)                    { (void)Token; }
    [[nodiscard]] virtual bool        QueryToggleSupported() const noexcept                 { return false; }
    [[nodiscard]] virtual bool        QueryToggleEnabled() const noexcept                   { return true; }
    virtual void                      ConfigureToggle(bool Enabled)                         { (void)Enabled; }
    [[nodiscard]] virtual uint64_t    QueryRevision() const noexcept                        { return 0; }
    virtual void                      EnumerateSwitches(std::vector<InspectorSwitchEntry>& OutSwitches) { (void)OutSwitches; }
    virtual void                      ConstructInspector()                                  = 0;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    INSPECTOR RUNTIME
//------------------------------------------------------------------------------------------------------------------------

class InspectorRuntime
{
  public:
    [[nodiscard]] static InspectorRuntime& Instance() noexcept;

    InspectorRuntime(const InspectorRuntime&)            = delete;
    InspectorRuntime& operator=(const InspectorRuntime&) = delete;

    // Replaces all inspectors on Token with one primary inspector.
    bool BindInspector(RecordToken Token, std::unique_ptr<RecordInspector> Inspector);

    // Appends an additional modular RecordInspector (e.g. PhysicsInspector, ThermalInspector, ChemicalInspector).
    bool AttachInspector(RecordToken Token, std::unique_ptr<RecordInspector> Inspector);

    [[nodiscard]] RecordInspector*       FindInspector(RecordToken Token) noexcept;
    [[nodiscard]] const RecordInspector* FindInspector(RecordToken Token) const noexcept;
    [[nodiscard]] size_t                 QueryInspectorCount(RecordToken Token) const noexcept;
    [[nodiscard]] bool                   QueryHasEditor(RecordToken Token) const noexcept;
    [[nodiscard]] bool                   QuerySlotOpen(RecordToken Token) const noexcept;
    [[nodiscard]] uint64_t               QuerySlotCount() const noexcept;
    [[nodiscard]] uint64_t               QueryBoundCount() const noexcept;

    // Central Inspector dispatch: renders all modular inspectors attached to Token and signals deletion requests.
    void InspectRecord(RecordToken Token, bool& OutDeleteRequested);

    // Standalone *Editor activation (FractureEditor, FluidPlumeEditor, ParticleEditor, PolygonClusterEditor, or custom).
    bool                      RequestEditorOpen(RecordToken Token) noexcept;
    void                      CloseActiveEditor() noexcept;
    [[nodiscard]] RecordToken QueryActiveEditorToken() const noexcept;
    void                      ConstructActiveEditor();

  private:
    friend class RecordEntry;

    InspectorRuntime()  = default;
    ~InspectorRuntime() = default;

    void OpenSlot(RecordToken Token);
    void Unregister(RecordToken Token) noexcept;

#if FRONTIER_EDITOR
    struct SlotEntry
    {
        std::vector<std::unique_ptr<RecordInspector>> Inspectors;
    };

    std::unordered_map<uint64_t, SlotEntry> Slots;
    RecordToken                             ActiveEditorToken{};
#endif
};

} // namespace Frontier
