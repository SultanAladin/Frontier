/*==========================================================================================================================================
                                                              STACKGLYPHS.JS
==========================================================================================================================================*/
// 🧩 Consistent monochrome editor glyphs and geometric operation symbols.

import {
    Mountain, MountainSnow, Layers, Columns3, MoveHorizontal, Rainbow, Sun, Lock, Eye, EyeOff,
    Contrast, Check, Wind, Search, Plus, X, Undo2, Redo2, Download, Focus, Box, Brush, Eraser,
    SquareDashed, Hand, Orbit, Waves, ChevronRight, ArrowRight, ArrowUpRight, ArrowUp, ArrowDown,
    Copy, Trash2, RotateCcw, SlidersHorizontal
} from 'lucide';

const Glyphs = {
    mountain: Mountain, ridge: MountainSnow, layers: Layers, strata: Layers, stacks: Columns3,
    cliff: MoveHorizontal, cave: Rainbow, sun: Sun, lock: Lock, eye: Eye, 'eye-off': EyeOff,
    mask: Contrast, check: Check, wind: Wind, search: Search, plus: Plus, close: X,
    undo: Undo2, redo: Redo2, download: Download, focus: Focus, box: Box, brush: Brush,
    eraser: Eraser, select: SquareDashed, hand: Hand, orbit: Orbit, waves: Waves,
    'chevron-right': ChevronRight, 'arrow-right': ArrowRight, 'arrow-up-right': ArrowUpRight,
    'arrow-up': ArrowUp, 'arrow-down': ArrowDown, copy: Copy, trash: Trash2,
    reset: RotateCcw, sliders: SlidersHorizontal
};

export function StackGlyph(Token)
{
    const SVG = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    for (const [Name, Setting] of Object.entries({
        viewBox: '0 0 24 24', width: 18, height: 18, fill: 'none', stroke: 'currentColor',
        'stroke-width': 1.5, 'stroke-linecap': 'round', 'stroke-linejoin': 'round', 'aria-hidden': 'true'
    })) SVG.setAttribute(Name, Setting);
    for (const [Tag, Attributes] of Glyphs[Token] || Mountain)
    {
        const Shape = document.createElementNS('http://www.w3.org/2000/svg', Tag);
        for (const [Name, Setting] of Object.entries(Attributes)) Shape.setAttribute(Name, Setting);
        SVG.append(Shape);
    }
    return SVG;
}

export function StackAppearance(Type)
{
    if (Type === 'environment') return { Glyph: 'waves', Color: '#b7b7b7' };
    if (Type === 'foundation') return { Glyph: 'layers', Color: '#a0a0a0' };
    if (Type === 'tunnel' || Type === 'arch') return { Glyph: 'cave', Color: '#b7b7b7' };
    if (Type === 'stacks') return { Glyph: 'stacks', Color: '#b7b7b7' };
    if (Type === 'strata') return { Glyph: 'strata', Color: '#b7b7b7' };
    if (Type === 'fault') return { Glyph: 'cliff', Color: '#b7b7b7' };
    if (Type === 'sand-dunes' || Type === 'dunes') return { Glyph: 'wind', Color: '#b7b7b7' };
    return { Glyph: ['ridges', 'ridge-noise', 'fractal-ridges', 'ridged-multifractal'].includes(Type) ? 'ridge' : 'mountain', Color: '#b7b7b7' };
}

export function InitializeGlyphs(Root = document)
{
    Root.querySelectorAll('[data-glyph]').forEach(Caption => Caption.replaceChildren(StackGlyph(Caption.dataset.glyph)));
}
