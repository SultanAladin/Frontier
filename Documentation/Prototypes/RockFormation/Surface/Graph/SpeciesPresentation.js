/*====================================================================================================================================
                                                   SPECIESPRESENTATION.JS
====================================================================================================================================*/
// 🧩 The bridge between RockFormation's species table and the ported editor's catalogue shape

// 📝 The reference editor's modules import CatalogueSpecifications.js for four things: a port-colour
//    class, a per-category glyph, the spawnable roster, and the two form predicates. This file supplies
//    all four DERIVED FROM ConstructionSpecificationTable, so the species table stays the single source
//    of truth and the ported modules need no edits to their import list beyond the file naming.
//
//    🔴 Nothing here is authored twice. Every roster row is computed from a species, so adding a species
//       to the table makes it spawnable with no edit here. A hand-written roster would silently drift:
//       the catalogue would keep offering a species after it was renamed, and the spawn would throw
//       `unknown species` from InsertEntry — an error whose message points nowhere near this file.

import { ConstructionSpecificationTable } from "../../Construction/ConstructionSpecifications.js";
import { CatalogueFamilyOrder, FamilySummary, PortCategories } from "../../Construction/PortCategories.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                   PORT CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The reference keys dot colour by a WGSL-ish type naming (float/vec3/texture). RockFormation keys it
//    by MEANING instead — the six PortCategories. The class naming stays in the reference's `Dot*` form
//    because EditorSkin's stylesheet selects on it verbatim.
export const PortDotClass =
{
    Distance:   'DotDistance',
    Resistance: 'DotResistance',
    Warp:       'DotWarp',
    Tint:       'DotTint',
    Scalar:     'DotScalar',
    Weather:    'DotWeather',
    any:        'DotAny'
};

export function ResolvePortDotClass(PortClassification)
{
    return PortDotClass[PortClassification] || PortDotClass.any;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CATEGORY PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 Keyed by FAMILY, not by port category — a family is what the author picks from, and two families can
//    yield the same category (Mass and Combination both yield Distance) while wanting different glyphs.
export const CategoryPresentation =
{
    Mass:        { Glyph: 'Box',        GlyphClass: 'GlyphMass' },
    Resistance:  { Glyph: 'Strata',     GlyphClass: 'GlyphResistance' },
    Weather:     { Glyph: 'Sparkles',   GlyphClass: 'GlyphWeather' },
    Warp:        { Glyph: 'Branching',  GlyphClass: 'GlyphWarp' },
    Combination: { Glyph: 'Calculator', GlyphClass: 'GlyphCombination' },
    Tint:        { Glyph: 'Raster',     GlyphClass: 'GlyphTint' },
    Resolve:     { Glyph: 'Sun',        GlyphClass: 'GlyphResolve' }
};

export function ResolveFamilyPresentation(Family)
{
    return CategoryPresentation[Family] || CategoryPresentation.Mass;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   SPECIES CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Built once at module load, in CatalogueFamilyOrder, in the reference's category/items shape. The
//    reference's `Token` is the SPECIES KEY, which is what InsertEntry takes — so a spawn is a direct
//    handoff with no lookup table in between.
function ComposeSpeciesCatalogue()
{
    const Roster = [];

    for (const Family of CatalogueFamilyOrder)
    {
        const Items = Object.entries(ConstructionSpecificationTable)
            .filter(([, Specification]) => Specification.Family === Family)
            .map(([SpeciesKey, Specification]) => ({
                Token:         SpeciesKey,
                Naming:        Specification.Naming,
                Summary:       Specification.Summary,
                Badge:         Specification.Glyph,               // the 3-5 letter species tag
                Singular:      Specification.Singular === true,
                OutboundPort:  Specification.Yields || null,
                InboundPorts:  Specification.Intakes.map(Intake => Intake.Category)
            }));

        if (Items.length === 0) continue;

        const Presentation = ResolveFamilyPresentation(Family);
        Roster.push({
            Category: Family,
            Glyph:    Presentation.Glyph,
            Naming:   Family,
            Summary:  FamilySummary[Family] || '',
            Items:    Items
        });
    }

    return Roster;
}

export const NodeCatalogue = ComposeSpeciesCatalogue();

// 📝 The reference labels spawned ports "In 1..N" with a per-token override table. Here the labels are
//    the INTAKE NAMINGS, which are never optional and never guessed.
//
//    🔴 The returned label is display only. Transcribe() resolves by IntakeNaming carried on the port
//       record, so renaming a label here can never break operand resolution.
export function ResolveInboundLabels(SpeciesKey)
{
    const Specification = ConstructionSpecificationTable[SpeciesKey];
    if (!Specification) return [];
    return Specification.Intakes.map(Intake => Intake.Naming);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ENTRY FORM SELECTION
//------------------------------------------------------------------------------------------------------------------------

// 📝 The reference has three forms: full card, circular operator, and input regulator. RockFormation uses
//    ONLY the full card, because every species carries dials and a preview thumbnail and neither fits in
//    a 56 px circle.
//
//    🔴 These four exports are what keep the reference's compact-form code COMPILING AND DORMANT rather
//       than deleted. Returning empty arrays makes every `.includes()` in EntryRasterization false, so
//       the operator and regulator branches are unreachable — but they are still there, still correct,
//       and re-enabled by an Affordances.Form flag rather than by re-porting them from the reference.
export const OperatorTokens  = [];
export const RegulatorTokens = [];
export const AxisTokens      = [];
export const SweepTokens     = [];

// Every species renders as the full card.
export function CompactEntryCondition()
{
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     AFFORDANCES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Which of the reference's per-card actions a species offers. The rasterizer keeps the reference's
//    markup verbatim and simply does not reach a branch whose flag is false.
//
//    🔴 AttachInbound/AttachOutbound/DetachOutbound and PortScalar are OFF for every species, and that is
//       not a styling choice. An intake is a NAMED OPERAND that Transcribe() reads by name; a port added
//       at runtime would be named "In 3", match no intake, and draw a link that resolves to nothing —
//       a live wire on screen feeding a shader that never reads it.
export const DefaultAffordances =
{
    Form:           'full',
    Fold:           true,
    Preview:        true,
    Immobilize:     true,
    Annotate:       true,
    Notice:         true,
    Reclaim:        true,
    Bypass:         true,

    Tack:           false,      // inert even in the reference — it sets a flag nothing reads
    Export:         false,      // no export target exists in this prototype
    AttachInbound:  false,      // 🔴 see the hazard above
    AttachOutbound: false,
    DetachOutbound: false,
    PortScalar:     false
};

export function ResolveAffordances(SpeciesKey)
{
    const Specification = ConstructionSpecificationTable[SpeciesKey];
    if (!Specification) return { ...DefaultAffordances };

    const Merged = { ...DefaultAffordances, ...(Specification.Affordances || {}) };

    // 🔴 The root is Singular, so it must never offer Reclaim. Deleting it leaves a tree with no
    //    resolve entry, which transcribes to an empty shader and renders black — with no error anywhere.
    if (Specification.Singular) Merged.Reclaim = false;

    return Merged;
}

// 📝 The swatch a category paints its studs with, read straight from PortCategories so the two can
//    never disagree.
export function ResolveCategorySwatch(Category)
{
    const Record = PortCategories[Category];
    return Record ? Record.Swatch : 'var(--port-scalar)';
}
