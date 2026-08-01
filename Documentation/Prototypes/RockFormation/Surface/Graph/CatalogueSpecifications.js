//========================================================================================================================
//                                            CatalogueSpecifications.js                                           🧩
//========================================================================================================================
//
// 📝 The two names the verbatim EditorSurface.js imports from the reference's catalogue module.
//
//    The reference's version also owned the node catalogue, the category palette and the inbound labels.
//    All three already exist here as SpeciesPresentation.js, built from ConstructionSpecificationTable, so
//    this file re-exports rather than restating them — a second catalogue would be a second source of truth
//    for what a species is called and how many intakes it has.

import { ConstructionSpecificationTable } from "../../Construction/ConstructionSpecifications.js";

//------------------------------------------------------------------------------------------------------------------------
//                                                  PREVIEWABLE SPECIES
//------------------------------------------------------------------------------------------------------------------------
//
// 📝 The reference used TerrainTokens to answer one question: does the chosen card produce something the
//    viewport can draw? Its answer was a hard-coded list of six noise generators. Here the answer is a
//    property of the species — anything that yields a Distance is a body with a surface to trace.
//
//    🔴 DERIVED, never a literal list. The reference's hard-coded array is exactly the kind of thing that
//       goes stale: adding a species would leave its card unpreviewable with nothing to point at, because
//       the viewport would simply fall through to 'default' and draw the previous rock.
//
//    📝 Resistance, Warp, Weather and Tint are deliberately absent. A Warp yields a displaced POSITION and a
//       Resistance yields a hardness — neither has a surface, so previewing one alone is meaningless; the
//       reference's own ResolvePreviewEntry walks downstream to the consumer for precisely this case.
export const TerrainTokens = Object.keys(ConstructionSpecificationTable)
    .filter(Species => ConstructionSpecificationTable[Species].Yields === "Distance");

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENTRY FORM
//------------------------------------------------------------------------------------------------------------------------

// 📝 Re-exported from the affordance table, where the form of every species is already declared.
export { CompactEntryCondition, ResolveInboundLabels, CategoryPresentation, NodeCatalogue }
    from "./SpeciesPresentation.js";
