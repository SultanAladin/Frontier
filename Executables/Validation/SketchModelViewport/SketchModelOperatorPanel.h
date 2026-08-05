/*==============================================================================================================================================
                                                        SKETCHMODELOPERATORPANEL.H
==============================================================================================================================================*/
// 🧩 The Blender-style OPERATOR / REDO box, pinned top-left of the viewport canvas. After a Fillet/Chamfer commits, this box appears and lets the
//    last edit be re-adjusted WITHOUT re-picking the corner: an Amount entry (mm) drags the radius / distance live, and a Fillet↔Chamfer selector
//    swaps the outcome. Every change re-commits through the modal's retained (shape, corner) via ReapplySketchFilletModalCommit — idempotent, so
//    dragging the amount never stacks arcs. Styled EXACTLY like ControlsGallery (Frontier PropertyCard + ScalarEntry + SelectionEntry) so it reads
//    as one of the app's own control cards. It draws nothing while there is no retained commit (SketchModelFilletModal.LastCommitValid == false).

#pragma once
#ifndef FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELOPERATORPANEL_H
#define FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELOPERATORPANEL_H

#include "imgui.h"

namespace Frontier { struct ThemeConfiguration; struct ParametricSketchShapeStore; }

namespace SketchModelViewportValidation
{

struct SketchModelFilletModal;
struct SketchModelInsetModal;

//------------------------------------------------------------------------------------------------------------------------
//                                                       PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Render the operator/redo box for one cycle, pinned near the top-left of the canvas rect. Draws nothing (and returns false) unless a Fillet/
//    Chamfer commit is retained (Modal.LastCommitValid). On an Amount drag or a Fillet↔Chamfer swap it re-applies the retained edit through
//    ReapplySketchFilletModalCommit and returns true (so the caller knows the box owned the interaction this frame and can veto a canvas pick under
//    the box). Its own hover / interaction is fenced to the box rect. Call from inside the canvas child, over the sheets, after the modal advance.
bool ConstructSketchModelOperatorPanel(const Frontier::ThemeConfiguration&   Theme,
                                       SketchModelFilletModal&               Modal,
                                       Frontier::ParametricSketchShapeStore& Store,
                                       ImVec2 CanvasOrigin, ImVec2 CanvasSize);

// 📝 Render the OFFSET operator/redo box — the Inset-modal twin of the Fillet box. Draws nothing (and returns false) unless an Offset commit is
//    retained (Modal.LastCommitValid). Its two rows re-adjust the last offset WITHOUT re-dragging: a Distance entry (signed mm — negative deflates)
//    and a Corners dropdown (Round / Miter / Bevel). Any change re-runs ReapplySketchInsetModalCommit on the retained sources (which detaches its own
//    prior Profiles first, so a slide never stacks) and returns true so the caller can veto a canvas pick under the box. StackBelow shifts it down by
//    that many pixels so it sits UNDER the Fillet box when both are retained (both pin top-left); pass 0 when it is the only box.
bool ConstructSketchModelInsetOperatorPanel(const Frontier::ThemeConfiguration&   Theme,
                                            SketchModelInsetModal&                Modal,
                                            Frontier::ParametricSketchShapeStore& Store,
                                            ImVec2 CanvasOrigin, ImVec2 CanvasSize,
                                            float StackBelow);

}   // namespace SketchModelViewportValidation

#endif   // FRONTIER_EXECUTABLES_VALIDATION_SKETCHMODELVIEWPORT_SKETCHMODELOPERATORPANEL_H
