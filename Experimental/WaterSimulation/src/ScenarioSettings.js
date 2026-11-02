/*====================================================================================================================================
                                                    SCENARIO SETTINGS.JS
====================================================================================================================================*/
// 🧩 Default settings, the three surface model descriptors, quality profiles, and the control catalogue

export const DomainExtent = 400.0;
export const AtlasResolution = 256;

export const ModelDescriptors =
[
    {
        Index:  1,
        Label:  'Spectral ocean',
        Note:   'GPU FFT of a JONSWAP wind-sea, three tiled cascades. Best open-water realism.'
    },
    {
        Index:  2,
        Label:  'Gerstner crest sum',
        Note:   'Sixteen analytic trochoidal waves. Sharp crests, art-directed, cheapest model.'
    },
    {
        Index:  3,
        Label:  'Grid wave solve',
        Note:   'Global 512² finite-difference sea. Reacts to vessel, wind, shoaling, and impacts.'
    }
];

export const QualityProfiles =
[
    { Label: 'Performance', CascadeCount: 2, SprayRatio: 0.25, RippleDivisor: 2 },
    { Label: 'Balanced',    CascadeCount: 2, SprayRatio: 0.50, RippleDivisor: 1 },
    { Label: 'High',        CascadeCount: 3, SprayRatio: 0.75, RippleDivisor: 1 },
    { Label: 'Ultra',       CascadeCount: 3, SprayRatio: 1.00, RippleDivisor: 1 }
];

export const DefaultSettings =
{
    SimulationCategory:   0,
    QualityCategory:      2,
    WaveHeight:           1.45,
    WindSpeed:            9.5,
    WindAngle:            32.0,
    Choppiness:           0.85,
    WaveSpeed:            1.0,
    RippleEnabled:        true,
    RippleStrength:       1.0,
    FoamEnabled:          true,
    FoamIntensity:        0.8,
    SprayEnabled:         true,
    SprayDensity:         0.65,
    VesselEnabled:        true,
    VesselThrottle:       0.55,
    SunElevation:         13.0,
    SunAzimuth:           112.0,
    CloudCover:           0.42,
    SkyHaze:              0.30,
    WaterClarity:         0.55,
    MicroDetail:          1.0,
    ReflectionEnabled:    true,
    RefractionEnabled:    true,
    SubsurfaceEnabled:    true,
    BloomEnabled:         true,
    Exposure:             1.0,
    RenderScale:          0.9,
    Antialias:            1.0,
    GridOverlay:          false,
    FieldView:            0,
    AutoOrbit:            false,
    SimulationPaused:     false
};

export const ControlSections =
[
    {
        Title: 'Surface model',
        Controls:
        [
            { Key: 'SimulationCategory', Label: 'Water simulation', Category: 'model', Hint: 'Keys 1 – 3 switch models live.' }
        ]
    },
    {
        Title: 'Sea state',
        Controls:
        [
            { Key: 'WaveHeight',  Label: 'Wave height',   Category: 'range', Min: 0.1, Max: 5.0, Step: 0.05, Unit: 'm' },
            { Key: 'WindSpeed',   Label: 'Wind speed',    Category: 'range', Min: 2.0, Max: 24.0, Step: 0.1, Unit: 'm/s' },
            { Key: 'WindAngle',   Label: 'Wind bearing',  Category: 'range', Min: 0.0, Max: 360.0, Step: 1.0, Unit: '°' },
            { Key: 'Choppiness',  Label: 'Choppiness',    Category: 'range', Min: 0.0, Max: 1.6, Step: 0.02 },
            { Key: 'WaveSpeed',   Label: 'Wave speed',    Category: 'range', Min: 0.2, Max: 2.2, Step: 0.02, Unit: '×' }
        ]
    },
    {
        Title: 'Ripples, foam, splash',
        Controls:
        [
            { Key: 'RippleEnabled',  Label: '2.5D ripple grid', Category: 'switch' },
            { Key: 'RippleStrength', Label: 'Ripple strength',  Category: 'range', Min: 0.0, Max: 3.0, Step: 0.05 },
            { Key: 'FoamEnabled',    Label: 'Grid foam',        Category: 'switch' },
            { Key: 'FoamIntensity',  Label: 'Foam intensity',   Category: 'range', Min: 0.0, Max: 2.0, Step: 0.02 },
            { Key: 'SprayEnabled',   Label: '3D spray particles', Category: 'switch' },
            { Key: 'SprayDensity',   Label: 'Spray emission',   Category: 'range', Min: 0.0, Max: 1.6, Step: 0.02 }
        ]
    },
    {
        Title: 'Scene',
        Controls:
        [
            { Key: 'VesselEnabled',  Label: 'Vessel (wake + spray)', Category: 'switch' },
            { Key: 'VesselThrottle', Label: 'Throttle',              Category: 'range', Min: 0.0, Max: 1.0, Step: 0.01 },
            { Key: 'WaterClarity',   Label: 'Water clarity',         Category: 'range', Min: 0.0, Max: 1.0, Step: 0.01 },
            { Key: 'SunElevation',   Label: 'Sun elevation',         Category: 'range', Min: 2.0, Max: 70.0, Step: 0.5, Unit: '°' },
            { Key: 'SunAzimuth',     Label: 'Sun bearing',           Category: 'range', Min: 0.0, Max: 360.0, Step: 1.0, Unit: '°' },
            { Key: 'CloudCover',     Label: 'Cloud cover',           Category: 'range', Min: 0.0, Max: 1.0, Step: 0.01 },
            { Key: 'SkyHaze',        Label: 'Atmosphere haze',       Category: 'range', Min: 0.0, Max: 1.0, Step: 0.01 }
        ]
    },
    {
        Title: 'Rendering',
        Controls:
        [
            { Key: 'QualityCategory',  Label: 'Quality', Category: 'segments', OptionLabels: ['Perf', 'Balanced', 'High', 'Ultra'] },
            { Key: 'ReflectionEnabled', Label: 'Mirror reflection',  Category: 'switch' },
            { Key: 'RefractionEnabled', Label: 'Screen refraction',  Category: 'switch' },
            { Key: 'SubsurfaceEnabled', Label: 'Crest scattering',   Category: 'switch' },
            { Key: 'BloomEnabled',      Label: 'Bloom',              Category: 'switch' },
            { Key: 'Exposure',          Label: 'Exposure',           Category: 'range', Min: 0.3, Max: 2.4, Step: 0.02 },
            { Key: 'RenderScale',       Label: 'Render scale',       Category: 'range', Min: 0.5, Max: 1.0, Step: 0.05, Hint: 'Drop below 1.0 on integrated GPUs.' },
            { Key: 'Antialias',         Label: 'Edge resolve',       Category: 'range', Min: 0.0, Max: 1.6, Step: 0.02 },
            { Key: 'MicroDetail',       Label: 'Micro ripple detail', Category: 'range', Min: 0.0, Max: 2.0, Step: 0.02 }
        ]
    },
    {
        Title: 'Inspection',
        Controls:
        [
            { Key: 'GridOverlay', Label: 'Global grid overlay', Category: 'switch', Hint: 'Shows the world-space solve lattice.' },
            { Key: 'FieldView',   Label: 'Field view', Category: 'segments', OptionLabels: ['Shaded', 'Height', 'Foam', 'Normal', 'Depth'] },
            { Key: 'AutoOrbit',   Label: 'Auto orbit camera', Category: 'switch' },
            { Key: 'SimulationPaused', Label: 'Freeze simulation', Category: 'switch' }
        ]
    }
];

export function ReadSetting(Settings, Key)
{
    return Settings[Key];
}

export function FormatControlValue(Control, Value)
{
    if (Control.Category === 'switch')
    {
        return Value ? 'On' : 'Off';
    }

    if (Control.Unit === '°' || Control.Unit === '×' || Control.Unit === 'm/s')
    {
        return `${Number(Value).toFixed(Control.Step < 0.1 ? 2 : 1)}${Control.Unit}`;
    }

    const Digits = Control.Step >= 1 ? 0 : (Control.Step >= 0.1 ? 1 : 2);
    return `${Number(Value).toFixed(Digits)}${Control.Unit ? ` ${Control.Unit}` : ''}`;
}
