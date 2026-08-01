/*====================================================================================================================================
                                                    ENTRYRASTERIZATION.JS
====================================================================================================================================*/
// 🧩 Builds the DOM for one graph entry — full-form node, circular operator, and input regulator

import { ComposeGlyph, TrigonometricCurves } from './GlyphOutlines.js';
import
{
    ResolvePortDotClass, OperatorTokens, AxisTokens, SweepTokens, ResolveFamilyPresentation
} from '../Graph/SpeciesPresentation.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                     HOVER TOOLBAR
//------------------------------------------------------------------------------------------------------------------------

function ComposeToolbarAction(ActionNaming, Hint, OutlineNaming, EngagedClass)
{
    return `<div class="ActionHint" data-hint="${Hint}">`
         +   `<button class="ToolbarAction ${EngagedClass || ''}" data-action="${ActionNaming}">`
         +     ComposeGlyph(OutlineNaming, 14)
         +   `</button>`
         + `</div>`;
}

// 📝 Every action is gated on Entry.Affordances. The reference emitted all eight unconditionally; here a
//    species that cannot offer an action does not render its button.
//
//    🔴 Rules are emitted only between two groups that BOTH survived the gate. Emitting them
//       unconditionally leaves a toolbar that opens or closes on a divider, or shows two adjacent rules
//       with nothing between them — which reads as a missing button rather than an absent affordance.
function ComposeFullToolbar(Entry)
{
    const Affordances = Entry.Affordances || {};

    const FoldHint  = Entry.Folded ? 'Expand' : 'Collapse';
    const FoldGlyph = Entry.Folded ? 'Expand' : 'Contract';
    const LockHint  = Entry.Immobilized ? 'Unlock Node' : 'Lock Node';
    const LockGlyph = Entry.Immobilized ? 'Fastened' : 'Unfastened';

    const Groups = [];

    const ShapeGroup = [];
    if (Affordances.Fold)
    {
        ShapeGroup.push(ComposeToolbarAction('Fold', FoldHint, FoldGlyph, Entry.Folded ? 'Engaged' : ''));
    }
    if (Affordances.Bypass)
    {
        ShapeGroup.push(ComposeToolbarAction('Bypass', Entry.Bypassed ? 'Enable' : 'Bypass', 'Suspend',
                                            Entry.Bypassed ? 'EngagedAmber' : ''));
    }
    if (ShapeGroup.length > 0) Groups.push(ShapeGroup.join(''));

    const ObserveGroup = [];
    if (Affordances.Preview) ObserveGroup.push(ComposeToolbarAction('Preview', 'Preview', 'Observe'));
    if (Affordances.Tack)
    {
        ObserveGroup.push(ComposeToolbarAction('Tack', Entry.Tacked ? 'Unpin' : 'Pin', 'Tack',
                                              Entry.Tacked ? 'EngagedIndigo' : ''));
    }
    if (ObserveGroup.length > 0) Groups.push(ObserveGroup.join(''));

    const MarkGroup = [];
    if (Affordances.Immobilize)
    {
        MarkGroup.push(ComposeToolbarAction('Immobilize', LockHint, LockGlyph,
                                           Entry.Immobilized ? 'EngagedAmber' : ''));
    }
    if (Affordances.Annotate) MarkGroup.push(ComposeToolbarAction('Annotate', 'Comment', 'Annotation'));
    if (Affordances.Notice)   MarkGroup.push(ComposeToolbarAction('Notice', 'Set Tooltip', 'Notice'));
    if (MarkGroup.length > 0) Groups.push(MarkGroup.join(''));

    if (Affordances.Reclaim)
    {
        Groups.push(ComposeToolbarAction('Reclaim', 'Delete', 'Trash', 'Destructive'));
    }

    return `<div class="EntryToolbar">`
         +   Groups.join(`<div class="ToolbarRule"></div>`)
         + `</div>`;
}

function ComposeCompactToolbar()
{
    return `<div class="EntryToolbar">`
         +   ComposeToolbarAction('Preview', 'Preview', 'Observe')
         +   ComposeToolbarAction('Annotate', 'Comment', 'Annotation')
         +   `<div class="ToolbarRule"></div>`
         +   ComposeToolbarAction('Reclaim', 'Delete', 'Trash', 'Destructive')
         + `</div>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  SHARED DECORATIONS
//------------------------------------------------------------------------------------------------------------------------

function ComposeExportBadge(Entry, CompactCondition)
{
    if (!Entry.Exported) return '';
    const BadgeClass = CompactCondition ? 'ExportBadge Compact' : 'ExportBadge';
    const BadgeText  = CompactCondition ? 'Exp' : 'Export';
    return `<button class="${BadgeClass}" data-action="Export">${BadgeText}</button>`;
}

function ComposeNotice(Entry)
{
    if (!Entry.Notice) return '';
    return `<div class="EntryTooltip">${EscapeMarkup(Entry.Notice)}</div>`;
}

function ComposeAnnotationPane(Entry, CompactCondition)
{
    if (Entry.Annotation === undefined && !Entry.Composing) return '';

    const PaneClass = CompactCondition ? 'CommentPane Compact' : 'CommentPane';
    if (Entry.Composing)
    {
        return `<div class="${PaneClass} Composing">`
             +   `<textarea class="CommentField" data-action="AnnotationField" rows="2" `
             +   `placeholder="Add a comment...">${EscapeMarkup(Entry.Annotation || '')}</textarea>`
             + `</div>`;
    }

    const AnnotationBody = Entry.Annotation
        ? EscapeMarkup(Entry.Annotation)
        : `<span class="Vacant">Empty comment...</span>`;
    return `<div class="${PaneClass}">`
         +   `<div class="CommentBody" data-action="AnnotationOpen">${AnnotationBody}</div>`
         + `</div>`;
}

export function EscapeMarkup(RawText)
{
    return String(RawText)
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;').replace(/'/g, '&#39;');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    FULL-FORM ENTRY
//------------------------------------------------------------------------------------------------------------------------

// 📝 An optional intake is marked so the author can tell "not wired yet" from "not wired and that is fine".
//    Without the mark, an unlinked optional intake looks identical to a missing required operand.
function ComposeInboundRow(Entry, Port, PortIndex)
{
    const DotClass    = ResolvePortDotClass(Port.Classification);
    const Affordances = Entry.Affordances || {};

    let Affordance = '';
    if (Affordances.PortScalar)
    {
        const ScalarRevealed = Entry.RevealedScalars[Port.Token];
        Affordance = ScalarRevealed
            ? `<input class="PinScalarField" type="text" data-action="PortScalar" data-port="${Port.Token}" `
            + `value="${EscapeMarkup(Entry.PortScalars[Port.Token] || '')}" placeholder="0.0" `
            + `title="Double click to remove value">`
            : `<button class="PinAffordance" data-action="PortScalarReveal" data-port="${Port.Token}" title="Set Value">`
            + ComposeGlyph('Minus', 10) + `</button>`;
    }
    else if (Port.Optional)
    {
        Affordance = `<span class="PinOptional" title="optional intake">·</span>`;
    }

    return `<div class="PinRow">`
         +   `<div class="PinDot Inbound ${DotClass}" data-porthit="target" `
         +   `data-entry="${Entry.Token}" data-port="${Port.Token}" `
         +   `title="${EscapeMarkup(Port.Classification)}"></div>`
         +   `<span class="PinLabel">${EscapeMarkup(Port.Label || `In ${PortIndex + 1}`)}</span>`
         +   Affordance
         + `</div>`;
}

function ComposeOutboundRow(Entry, Port, PortIndex)
{
    const DotClass    = ResolvePortDotClass(Port.Classification);
    const Affordances = Entry.Affordances || {};

    const Severing = Affordances.DetachOutbound
        ? `<button class="PinAffordance Severing" data-action="DetachOutbound" data-port="${Port.Token}" `
        + `title="Remove Pin">` + ComposeGlyph('Minus', 10) + `</button>`
        : '';

    return `<div class="PinRow Outbound">`
         +   Severing
         +   `<span class="PinLabel">${EscapeMarkup(Port.Label || `Out ${PortIndex + 1}`)}</span>`
         +   `<div class="PinDot Outbound ${DotClass}" data-porthit="source" `
         +   `data-entry="${Entry.Token}" data-port="${Port.Token}" `
         +   `title="${EscapeMarkup(Port.Classification)}"></div>`
         + `</div>`;
}

// 📝 A card carries NO dials and NO thumbnail. It is the reference's shape exactly: head, then the two
//    port columns. Dials are authored in the MarchDials / WeatherDials side panel, which already reads
//    the same Entry.Dials array, so nothing about the three-speed contract changes.
//
//    🔴 DO NOT REINTRODUCE EITHER ONE HERE. A slider inside the card sits on top of the drag handle, so
//       every dial haul also moves the card unless the pointer handler special-cases it; and a per-card
//       WebGPU canvas makes the wholesale RefreshEntries() rebuild below unsafe, because the host holds
//       the canvas by reference and would paint into a detached element -- a thumbnail that silently
//       stops updating, with no error raised anywhere. The side panel has neither problem.

function ComposeFullEntry(Entry)
{
    const Affordances  = Entry.Affordances || {};
    const InboundRows  = Entry.InboundPorts.map((Port, Index) => ComposeInboundRow(Entry, Port, Index)).join('');
    const OutboundRows = Entry.OutboundPorts.map((Port, Index) => ComposeOutboundRow(Entry, Port, Index)).join('');

    const ImmobilizedMark = Entry.Immobilized
        ? `<span style="color:rgba(234,179,8,.7);display:flex">${ComposeGlyph('Fastened', 12, 2.5)}</span>`
        : '';

    const AttachInbound = Affordances.AttachInbound
        ? `<button class="CaptionAdd" data-action="AttachInbound" title="Add Input Pin">`
        + ComposeGlyph('Plus', 12) + `</button>`
        : '';
    const AttachOutbound = Affordances.AttachOutbound
        ? `<button class="CaptionAdd" data-action="AttachOutbound" title="Add Output Pin">`
        + ComposeGlyph('Plus', 12) + `</button>`
        : '';

    // 📝 The species badge — the 3-5 letter tag from Specification.Glyph. It sits beside the family glyph
    //    so two species of one family stay distinguishable when the card is folded.
    const SpeciesBadge = Entry.Badge
        ? `<span class="SpeciesBadge">${EscapeMarkup(Entry.Badge)}</span>`
        : '';

    // 🔴 An outbound column with no ports must not render its caption. SurfaceResolve is the only species
    //    with no yield, and a bare "Out" heading over nothing reads as a port that failed to draw.
    const OutboundColumn = Entry.OutboundPorts.length > 0
        ? `<div class="PinColumn Outbound">`
        +   `<div class="PinCaption">${AttachOutbound}<span>Out</span></div>`
        +   OutboundRows
        + `</div>`
        : '';

    // 📝 Folding hides the ports, keeping only the head — so a settled part of a large tree collapses to a
    //    title bar without leaving the links it carries unrouted.
    const FoldedInterior = Entry.Folded
        ? ''
        : `<div class="PinRegion">`
        +   `<div class="PinColumn">`
        +     `<div class="PinCaption"><span>In</span>${AttachInbound}</div>`
        +     InboundRows
        +   `</div>`
        +   OutboundColumn
        + `</div>`;

    // 🔴 Even when folded, the ports must still exist in the DOM or ResolvePortAnchor finds nothing and
    //    every link touching a folded card vanishes. They are moved to a zero-height stub that keeps the
    //    hit targets addressable at the card's edge.
    const FoldedStubs = Entry.Folded
        ? `<div class="FoldedStubs">`
        +   Entry.InboundPorts.map(Port =>
              `<div class="PinDot Inbound ${ResolvePortDotClass(Port.Classification)}" data-porthit="target" `
            + `data-entry="${Entry.Token}" data-port="${Port.Token}"></div>`).join('')
        +   Entry.OutboundPorts.map(Port =>
              `<div class="PinDot Outbound ${ResolvePortDotClass(Port.Classification)}" data-porthit="source" `
            + `data-entry="${Entry.Token}" data-port="${Port.Token}"></div>`).join('')
        + `</div>`
        : '';

    // 📝 Entry.Glyph carries the FAMILY key; the outline naming and the colour class are both resolved
    //    from it, so a new family gets a glyph by adding one row to CategoryPresentation.
    const Presentation = ResolveFamilyPresentation(Entry.Glyph);

    return `<div class="EntryBody" data-action="EntryBody">`
         +   ComposeExportBadge(Entry, false)
         +   ComposeFullToolbar(Entry)
         +   ComposeNotice(Entry)
         +   `<div class="EntryHead">`
         +     `<div class="HeadNaming">`
         +       `<div class="HeadGlyph ${Presentation.GlyphClass}">`
         +         ComposeGlyph(Presentation.Glyph, 16)
         +       `</div>`
         +       `<div class="HeadText">`
         +         `<span class="HeadTitle">${EscapeMarkup(Entry.Title)}</span>`
         +         (Entry.Subtitle ? `<span class="HeadSubtitle">${EscapeMarkup(Entry.Subtitle)}</span>` : '')
         +       `</div>`
         +     `</div>`
         +     `<div style="display:flex;align-items:center;gap:8px">${SpeciesBadge}${ImmobilizedMark}</div>`
         +   `</div>`
         +   FoldedInterior
         +   FoldedStubs
         +   ComposeAnnotationPane(Entry, false)
         + `</div>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 COMPACT (MATH) ENTRY
//------------------------------------------------------------------------------------------------------------------------

// The centred symbol of a circular or pill-shaped math operator.
function ComposeOperatorGlyph(CatalogueToken)
{
    switch (CatalogueToken)
    {
        case 'math_add':   return ComposeGlyph('Plus', 24);
        case 'math_sub':   return ComposeGlyph('Minus', 24);
        case 'math_mult':  return `<span class="Symbol">&times;</span>`;
        case 'math_div':   return `<span class="Symbol">&divide;</span>`;
        case 'math_log':   return `<span class="Word">log</span>`;
        case 'math_clamp': return `<span class="WordWide">clamp</span>`;
        case 'math_sine':  return TrigonometricCurves.Sine;
        case 'math_cos':   return TrigonometricCurves.Cosine;
        default:           return '';
    }
}

function ComposeAxisFields(Entry)
{
    const AxisSpecifications = [
        { Axis: 'X', Class: 'AxisX' },
        { Axis: 'Y', Class: 'AxisY' }
    ];
    if (Entry.CatalogueToken === 'input_vec3') AxisSpecifications.push({ Axis: 'Z', Class: 'AxisZ' });

    const Fields = AxisSpecifications.map((Specification) =>
    {
        const AxisKey = Specification.Axis.toLowerCase();
        return `<input class="AxisField ${Specification.Class}" type="text" data-action="AxisScalar" `
             + `data-axis="${AxisKey}" placeholder="${Specification.Axis}" `
             + `value="${EscapeMarkup(Entry.PortScalars[AxisKey] || '')}">`;
    }).join('');

    return `<div class="AxisRow">${Fields}</div>`;
}

function ComposeRadialDial(Entry)
{
    const DialAngle = parseFloat(Entry.PortScalars['val'] || '0') || 0;
    return `<div class="RadialDial" data-action="RadialDial">`
         +   `<div class="DialPivot"></div>`
         +   `<div class="DialNeedle" style="transform:translate(0,-50%) rotate(${DialAngle}deg)"></div>`
         + `</div>`;
}

function ComposeRegulatorCore(Entry)
{
    if (AxisTokens.includes(Entry.CatalogueToken))
    {
        return ComposeAxisFields(Entry);
    }

    const ScalarField = `<input class="RegulatorField" type="text" data-action="RegulatorScalar" `
                      + `placeholder="0.0" value="${EscapeMarkup(Entry.PortScalars['val'] || '')}">`;

    let Auxiliary = '';
    if (SweepTokens.includes(Entry.CatalogueToken) && Entry.CatalogueToken === 'input_slider')
    {
        const SweepPosition = parseFloat(Entry.PortScalars['val'] || '0') || 0;
        Auxiliary = `<input class="SweepField" type="range" min="0" max="10" step="0.1" `
                  + `data-action="RegulatorSweep" value="${SweepPosition}">`;
    }
    else if (Entry.CatalogueToken === 'input_radial')
    {
        Auxiliary = ComposeRadialDial(Entry);
    }

    return `<div style="display:flex;flex-direction:column;width:100%">${ScalarField}${Auxiliary}</div>`;
}

function ComposeCompactEntry(Entry)
{
    const OperatorCondition  = OperatorTokens.includes(Entry.CatalogueToken);
    const RegulatorCondition = AxisTokens.includes(Entry.CatalogueToken)
                            || SweepTokens.includes(Entry.CatalogueToken)
                            || Entry.CatalogueToken === 'input_radial';

    let BodyClass = 'MathBody Compound';
    if (OperatorCondition)       BodyClass = 'MathBody Operator';
    else if (RegulatorCondition) BodyClass = 'MathBody Regulator';

    let CoreMarkup;
    if (RegulatorCondition)
    {
        CoreMarkup = `<div class="RegulatorStack">`
                   +   `<span class="RegulatorCaption">${EscapeMarkup(Entry.Title)}</span>`
                   +   ComposeRegulatorCore(Entry)
                   + `</div>`;
    }
    else
    {
        const WaveCaption = ['math_sine', 'math_cos'].includes(Entry.CatalogueToken)
            ? `<span class="WaveCaption">${Entry.CatalogueToken.replace('math_', '')}</span>`
            : '';
        CoreMarkup = `<div class="OperatorGlyph">${ComposeOperatorGlyph(Entry.CatalogueToken)}</div>${WaveCaption}`;
    }

    const InboundDots = Entry.InboundPorts.map((Port) =>
        `<div class="PinDot ${ResolvePortDotClass(Port.Classification)}" data-porthit="target" `
      + `data-entry="${Entry.Token}" data-port="${Port.Token}" title="${EscapeMarkup(Port.Label || '')}"></div>`).join('');

    const OutboundDots = Entry.OutboundPorts.map((Port) =>
        `<div class="PinDot ${ResolvePortDotClass(Port.Classification)}" data-porthit="source" `
      + `data-entry="${Entry.Token}" data-port="${Port.Token}" title="${EscapeMarkup(Port.Label || '')}"></div>`).join('');

    return `<div class="${BodyClass}" data-action="EntryBody">`
         +   ComposeExportBadge(Entry, true)
         +   ComposeCompactToolbar()
         +   ComposeNotice(Entry)
         +   `<div style="display:flex;flex-direction:column;align-items:center;justify-content:center;width:100%">`
         +     CoreMarkup
         +   `</div>`
         +   `<div class="MathPinColumn Inbound">${InboundDots}</div>`
         +   `<div class="MathPinColumn Outbound">${OutboundDots}</div>`
         +   ComposeAnnotationPane(Entry, true)
         + `</div>`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Rasterize one entry into a positioned wrapper element.
export function RasterizeEntry(Entry)
{
    const Wrapper = document.createElement('div');
    Wrapper.className = 'NodeEntry';
    Wrapper.dataset.entry = Entry.Token;
    Wrapper.style.transform = `translate(${Entry.PlaneX}px, ${Entry.PlaneY}px)`;

    if (Entry.Chosen)      Wrapper.classList.add('Chosen');
    if (Entry.Immobilized) Wrapper.classList.add('Immobile');
    if (Entry.Folded)      Wrapper.classList.add('Folded');
    if (Entry.Bypassed)    Wrapper.classList.add('Bypassed');
    if (Entry.Category)    Wrapper.classList.add(`Family${Entry.Category}`);

    Wrapper.innerHTML = Entry.Compact ? ComposeCompactEntry(Entry) : ComposeFullEntry(Entry);
    return Wrapper;
}
