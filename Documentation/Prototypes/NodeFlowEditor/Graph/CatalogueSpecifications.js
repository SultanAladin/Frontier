/*====================================================================================================================================
                                                 CATALOGUESPECIFICATIONS.JS
====================================================================================================================================*/
// 🧩 The spawnable node catalogue, port colour table, and per-category glyph assignment

//------------------------------------------------------------------------------------------------------------------------
//                                                    PORT CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

// 📝 A connection is admissible when either side is 'any', otherwise the classifications must match.
export const PortDotClass =
{
    float:     'DotFloat',
    vec2:      'DotVec2',
    vec3:      'DotVec3',
    color:     'DotColor',
    texture:   'DotTexture',
    heightmap: 'DotHeightmap',
    any:       'DotAny'
};

export function ResolvePortDotClass(PortClassification)
{
    return PortDotClass[PortClassification] || PortDotClass.any;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CATEGORY PRESENTATION
//------------------------------------------------------------------------------------------------------------------------

export const CategoryPresentation =
{
    input:     { Glyph: 'Pointer',    GlyphClass: 'GlyphInput' },
    generator: { Glyph: 'Sparkles',   GlyphClass: 'GlyphGenerator' },
    database:  { Glyph: 'Database',   GlyphClass: 'GlyphDatabase' },
    math:      { Glyph: 'Calculator', GlyphClass: 'GlyphMath' },
    router:    { Glyph: 'Branching',  GlyphClass: 'GlyphRouter' },
    texture:   { Glyph: 'Strata',     GlyphClass: 'GlyphTexture' },
    utility:   { Glyph: 'Configure',  GlyphClass: 'GlyphUtility' }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    NODE CATALOGUE
//------------------------------------------------------------------------------------------------------------------------

export const NodeCatalogue =
[
    {
        Category: 'input', Glyph: 'Pointer', Naming: 'Inputs & Controls',
        Items:
        [
            { Token: 'input_value',  Naming: 'Value (Float)',  Summary: 'Constant float value',   OutboundPort: 'float', InboundPorts: [] },
            { Token: 'input_slider', Naming: 'Slider',         Summary: '0-1 Value Slider',       OutboundPort: 'float', InboundPorts: [] },
            { Token: 'input_vec2',   Naming: 'Vector 2D',      Summary: 'X, Y values',            OutboundPort: 'vec2',  InboundPorts: [] },
            { Token: 'input_vec3',   Naming: 'Vector 3D',      Summary: 'X, Y, Z values',         OutboundPort: 'vec3',  InboundPorts: [] },
            { Token: 'input_radial', Naming: 'Radial Angles',  Summary: 'Radial slider',          OutboundPort: 'vec2',  InboundPorts: [] },
            { Token: 'input_box',    Naming: 'Box Control',    Summary: '2D coordinate box',      OutboundPort: 'vec2',  InboundPorts: [] }
        ]
    },
    {
        Category: 'generator', Glyph: 'Sparkles', Naming: 'Generators',
        Items:
        [
            { Token: 'gen_simplex',      Naming: 'Simplex Noise',      Summary: 'Continuous 2D/3D noise',        OutboundPort: 'heightmap', InboundPorts: ['float','float','float','float','float'] },
            { Token: 'gen_perlin',       Naming: 'Perlin Noise',       Summary: 'Classic gradient noise',        OutboundPort: 'heightmap', InboundPorts: ['float','float','float','float','float'] },
            { Token: 'gen_value',        Naming: 'Value Noise',        Summary: 'Smooth interpolated noise',     OutboundPort: 'heightmap', InboundPorts: ['float','float','float','float','float'] },
            { Token: 'gen_multifractal', Naming: 'MultiFractal',       Summary: 'Complex ridged noise',          OutboundPort: 'heightmap', InboundPorts: ['float','float','float','float','float'] },
            { Token: 'gen_cellular',     Naming: 'Cellular (Voronoi)', Summary: 'Distance-based cellular noise', OutboundPort: 'heightmap', InboundPorts: ['float','float','float','float','float'] },
            { Token: 'gen_white',        Naming: 'White Noise',        Summary: 'Random static noise',           OutboundPort: 'heightmap', InboundPorts: ['float'] }
        ]
    },
    {
        Category: 'texture', Glyph: 'Strata', Naming: 'Texturing',
        Items:
        [
            { Token: 'tex_satmap',  Naming: 'Satellite Map',  Summary: 'Satellite imagery texture',      OutboundPort: 'texture', InboundPorts: ['vec2'] },
            { Token: 'tex_terrain', Naming: 'Terrain Mask',   Summary: 'Slope/Height mask',              OutboundPort: 'texture', InboundPorts: ['heightmap'] },
            { Token: 'tex_normal',  Naming: 'Normal Map',     Summary: 'Generates normals from height',  OutboundPort: 'texture', InboundPorts: ['heightmap'] },
            { Token: 'tex_blend',   Naming: 'Texture Blend',  Summary: 'Blend multiple textures',        OutboundPort: 'texture', InboundPorts: ['texture','texture','heightmap'] }
        ]
    },
    {
        Category: 'math', Glyph: 'Calculator', Naming: 'Math & Logic',
        Items:
        [
            { Token: 'math_add',   Naming: 'Add',      Summary: 'Sum two inputs',           OutboundPort: 'float', InboundPorts: ['float','float'] },
            { Token: 'math_sub',   Naming: 'Subtract', Summary: 'Subtract inputs',          OutboundPort: 'float', InboundPorts: ['float','float'] },
            { Token: 'math_mult',  Naming: 'Multiply', Summary: 'Multiply two inputs',      OutboundPort: 'float', InboundPorts: ['float','float'] },
            { Token: 'math_div',   Naming: 'Divide',   Summary: 'Divide inputs',            OutboundPort: 'float', InboundPorts: ['float','float'] },
            { Token: 'math_clamp', Naming: 'Clamp',    Summary: 'Constrain value range',    OutboundPort: 'float', InboundPorts: ['float','float','float'] },
            { Token: 'math_sine',  Naming: 'Sine',     Summary: 'Trigonometric sine wave',  OutboundPort: 'float', InboundPorts: ['float'] },
            { Token: 'math_cos',   Naming: 'Cosine',   Summary: 'Trigonometric cos wave',   OutboundPort: 'float', InboundPorts: ['float'] },
            { Token: 'math_log',   Naming: 'Log',      Summary: 'Logarithm base 10',        OutboundPort: 'float', InboundPorts: ['float'] }
        ]
    },
    {
        Category: 'router', Glyph: 'Branching', Naming: 'Flow Control',
        Items:
        [
            { Token: 'router_branch', Naming: 'Branch',     Summary: 'If/Else logic flow',    OutboundPort: 'any', InboundPorts: ['float','any','any'] },
            { Token: 'router_merge',  Naming: 'Merge Flow', Summary: 'Combine flow paths',    OutboundPort: 'any', InboundPorts: ['any','any'] }
        ]
    },
    {
        Category: 'database', Glyph: 'Database', Naming: 'Data Sources',
        Items:
        [
            { Token: 'data_texture', Naming: 'Texture 2D',   Summary: 'Load an image texture', OutboundPort: 'texture', InboundPorts: [] },
            { Token: 'data_vector',  Naming: 'Vector Field', Summary: 'Load vector data',      OutboundPort: 'vec3',    InboundPorts: [] },
            { Token: 'data_mesh',    Naming: 'Mesh Data',    Summary: '3D Geometry payload',   OutboundPort: 'any',     InboundPorts: [] }
        ]
    },
    {
        Category: 'utility', Glyph: 'Configure', Naming: 'Utilities',
        Items:
        [
            { Token: 'util_timer',  Naming: 'Timer',       Summary: 'Tick based executions', OutboundPort: 'float', InboundPorts: [] },
            { Token: 'util_log',    Naming: 'Console Log', Summary: 'Debug values',          OutboundPort: 'any',   InboundPorts: ['any'] },
            { Token: 'util_viewer', Naming: 'Preview',     Summary: 'Value visualizer',      OutboundPort: 'any',   InboundPorts: ['any'] }
        ]
    }
];

//------------------------------------------------------------------------------------------------------------------------
//                                                 PORT LABEL OVERRIDES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Most spawned nodes label their inbound ports "In 1..N". These tokens name them by their role instead.
const InboundLabelOverrides =
{
    math_add:   ['A', 'B'],
    math_sub:   ['A', 'B'],
    math_mult:  ['A', 'B'],
    math_div:   ['A', 'B'],
    math_clamp: ['Value', 'Min', 'Max'],
    math_sine:  ['Value'],
    math_cos:   ['Value'],
    math_log:   ['Value'],
    tex_blend:  ['Tex A', 'Tex B', 'Mask'],
    gen_white:  ['Seed']
};

const GeneratorLabels = ['Scale', 'Octaves', 'Persistence', 'Lacunarity', 'Seed'];
const GeneratorTokens = ['gen_simplex', 'gen_perlin', 'gen_value', 'gen_multifractal', 'gen_cellular'];

export function ResolveInboundLabels(CatalogueToken, InboundTally)
{
    const Overrides = GeneratorTokens.includes(CatalogueToken)
        ? GeneratorLabels
        : InboundLabelOverrides[CatalogueToken];

    const Labels = [];
    for (let PortIndex = 0; PortIndex < InboundTally; PortIndex++)
    {
        Labels.push((Overrides && Overrides[PortIndex]) || `In ${PortIndex + 1}`);
    }
    return Labels;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  ENTRY CLASSIFICATION
//------------------------------------------------------------------------------------------------------------------------

export const OperatorTokens  = ['math_add', 'math_sub', 'math_mult', 'math_div'];
export const RegulatorTokens = ['input_value', 'input_slider', 'input_vec2', 'input_vec3', 'input_radial', 'input_box'];
export const AxisTokens      = ['input_vec2', 'input_vec3', 'input_box'];
export const SweepTokens     = ['input_slider', 'input_value'];
export const TerrainTokens   = ['gen_simplex', 'gen_perlin', 'gen_value', 'gen_multifractal', 'gen_cellular', 'gen_white'];

// A compact (math-shaped) entry is any math operation or any input regulator.
export function CompactEntryCondition(CatalogueToken)
{
    if (!CatalogueToken) return false;
    return CatalogueToken.startsWith('math_') || CatalogueToken.startsWith('input_');
}
