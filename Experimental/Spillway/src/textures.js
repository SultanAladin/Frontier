/*====================================================================================================================================
                                                          TEXTURES.JS
====================================================================================================================================*/
// 🧩 Canvas-authored surface detail: sun-cured concrete, desert grit, sheet water, signage, dust. No external image files.

import * as THREE from '../vendor/three.module.min.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       PALETTE
//------------------------------------------------------------------------------------------------------------------------

export const PALETTE = {
    concreteBase : '#ded6c3',
    concreteWarm : '#cdbfa4',
    concreteCool : '#b9b8ad',
    steelRust    : '#8a4a2f',
    steelDark    : '#3c4247',
    paintRed     : '#c04a2c',
    paintCream   : '#efe6cf',
    paintTeal    : '#1f6b66',
    waterShallow : '#3f8f86',
    waterDeep    : '#12504f',
    desert       : '#c2a173',
    sunLight     : '#ffd7a1',
    skyHorizon   : '#f0c58f',
    skyZenith    : '#6f7fa8',
    haze         : '#dcc4a4',
};

function Random(seed)
{
    let state = seed >>> 0;
    return () => {
        state = (state * 1664525 + 1013904223) >>> 0;
        return state / 4294967296;
    };
}

// Draws a blob and repeats it across the canvas edges so the texture tiles without a visible seam.
function WrappedRadial(context, x, y, radius, inner, outer, size)
{
    for (const dx of [-size, 0, size])
    {
        for (const dy of [-size, 0, size])
        {
            const gradient = context.createRadialGradient(x + dx, y + dy, 0, x + dx, y + dy, radius);
            gradient.addColorStop(0, inner);
            gradient.addColorStop(1, outer);
            context.fillStyle = gradient;
            context.beginPath();
            context.arc(x + dx, y + dy, radius, 0, Math.PI * 2);
            context.fill();
        }
    }
}

function MakeCanvas(size)
{
    const canvas = document.createElement('canvas');
    canvas.width = size;
    canvas.height = size;
    return canvas;
}

function FinishTexture(canvas, Repeat = 1)
{
    const texture = new THREE.CanvasTexture(canvas);
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.RepeatWrapping;
    texture.repeat.set(Repeat, Repeat);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.anisotropy = 8;
    return texture;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  POURED CONCRETE
//------------------------------------------------------------------------------------------------------------------------

// Form-board lines, tie holes, aggregate speckle, and alkali drips: reads as a 1960s spillway wall.
export function ConcreteTexture(size = 512)
{
    const canvas = MakeCanvas(size);
    const context = canvas.getContext('2d');
    const random = Random(90210);
    const unit = size / 512;

    context.fillStyle = PALETTE.concreteBase;
    context.fillRect(0, 0, size, size);

    for (let i = 0; i < 90; i++)
    {
        const warm = random() < 0.5;
        WrappedRadial(
            context,
            random() * size,
            random() * size,
            (20 + random() * 70) * unit,
            warm ? 'rgba(188,164,132,0.10)' : 'rgba(150,152,146,0.10)',
            'rgba(255,255,255,0)',
            size
        );
    }

    for (let i = 0; i < 9000; i++)
    {
        const shade = Math.floor(random() * 60);
        context.fillStyle = `rgba(${110 + shade},${105 + shade},${95 + shade},${0.04 + random() * 0.08})`;
        const dot = (0.6 + random() * 1.7) * unit;
        context.fillRect(random() * size, random() * size, dot, dot);
    }

    // Board-form joints.
    const joints = 4;
    for (let i = 1; i < joints; i++)
    {
        const y = (size / joints) * i;
        context.fillStyle = 'rgba(120,110,96,0.22)';
        context.fillRect(0, y, size, 1.6 * unit);
        context.fillStyle = 'rgba(255,252,242,0.30)';
        context.fillRect(0, y + 1.6 * unit, size, 1.2 * unit);
    }

    // Form tie holes.
    for (let row = 0; row < joints; row++)
    {
        for (let column = 0; column < 4; column++)
        {
            const x = (size / 4) * column + size / 8;
            const y = (size / joints) * row + size / 8;
            context.fillStyle = 'rgba(96,86,72,0.35)';
            context.beginPath();
            context.arc(x, y, 2.6 * unit, 0, Math.PI * 2);
            context.fill();
            context.fillStyle = 'rgba(255,255,255,0.25)';
            context.beginPath();
            context.arc(x - 0.8 * unit, y - 0.8 * unit, 1.4 * unit, 0, Math.PI * 2);
            context.fill();
        }
    }

    // Alkali streaks running down the wall.
    for (let i = 0; i < 16; i++)
    {
        const x = random() * size;
        const width = (2 + random() * 12) * unit;
        const gradient = context.createLinearGradient(0, 0, 0, size);
        gradient.addColorStop(0, 'rgba(255,252,240,0.20)');
        gradient.addColorStop(0.6, 'rgba(214,206,188,0.10)');
        gradient.addColorStop(1, 'rgba(150,140,120,0.0)');
        context.fillStyle = gradient;
        context.fillRect(x, random() * size * 0.4, width, size);
    }

    // Hairline cracks.
    context.strokeStyle = 'rgba(92,82,70,0.35)';
    context.lineWidth = 1.1 * unit;
    for (let i = 0; i < 7; i++)
    {
        let x = random() * size;
        let y = random() * size;
        context.beginPath();
        context.moveTo(x, y);
        for (let step = 0; step < 9; step++)
        {
            x += (random() - 0.5) * 36 * unit;
            y += (random() - 0.5) * 36 * unit;
            context.lineTo(x, y);
        }
        context.stroke();
    }

    return FinishTexture(canvas);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     DESERT FLOOR
//------------------------------------------------------------------------------------------------------------------------

export function DesertTexture(size = 512)
{
    const canvas = MakeCanvas(size);
    const context = canvas.getContext('2d');
    const random = Random(4242);
    const unit = size / 512;

    context.fillStyle = PALETTE.desert;
    context.fillRect(0, 0, size, size);

    for (let i = 0; i < 70; i++)
    {
        WrappedRadial(
            context,
            random() * size,
            random() * size,
            (30 + random() * 110) * unit,
            random() < 0.5 ? 'rgba(150,112,72,0.16)' : 'rgba(226,198,154,0.16)',
            'rgba(255,255,255,0)',
            size
        );
    }

    for (let i = 0; i < 6000; i++)
    {
        context.fillStyle = `rgba(${120 + random() * 90},${96 + random() * 70},${64 + random() * 50},${0.05 + random() * 0.09})`;
        context.fillRect(random() * size, random() * size, 1.4 * unit, 1.4 * unit);
    }

    // Wind ripple striations.
    context.lineWidth = 1.2 * unit;
    for (let i = 0; i < 40; i++)
    {
        const y = random() * size;
        context.strokeStyle = `rgba(${random() < 0.5 ? '150,116,74' : '232,206,164'},0.18)`;
        context.beginPath();
        for (let x = 0; x <= size; x += 8 * unit)
        {
            const wave = y + Math.sin((x / size) * Math.PI * (2 + random() * 2)) * 4 * unit;
            if (x === 0)
            {
                context.moveTo(x, wave);
            }
            else
            {
                context.lineTo(x, wave);
            }
        }
        context.stroke();
    }

    return FinishTexture(canvas);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHEET WATER
//------------------------------------------------------------------------------------------------------------------------

export function WaterTexture(size = 256)
{
    const canvas = MakeCanvas(size);
    const context = canvas.getContext('2d');
    const random = Random(8080);
    const unit = size / 256;

    context.fillStyle = PALETTE.waterShallow;
    context.fillRect(0, 0, size, size);

    for (let i = 0; i < 26; i++)
    {
        WrappedRadial(
            context,
            random() * size,
            random() * size,
            (18 + random() * 70) * unit,
            'rgba(20,72,72,0.30)',
            'rgba(255,255,255,0)',
            size
        );
    }

    context.lineWidth = 1.4 * unit;
    for (let i = 0; i < 60; i++)
    {
        const y = random() * size;
        context.strokeStyle = `rgba(228,248,240,${0.05 + random() * 0.16})`;
        context.beginPath();
        for (let x = 0; x <= size; x += 6 * unit)
        {
            const wave = y + Math.sin(x * 0.06 * unit + i) * 3 * unit;
            if (x === 0)
            {
                context.moveTo(x, wave);
            }
            else
            {
                context.lineTo(x, wave);
            }
        }
        context.stroke();
    }

    for (let i = 0; i < 500; i++)
    {
        context.fillStyle = `rgba(240,255,250,${0.08 + random() * 0.22})`;
        context.fillRect(random() * size, random() * size, 1.6 * unit, 1.0 * unit);
    }

    return FinishTexture(canvas);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      DUST SPRITE
//------------------------------------------------------------------------------------------------------------------------

export function DustSprite(size = 64)
{
    const canvas = MakeCanvas(size);
    const context = canvas.getContext('2d');
    const gradient = context.createRadialGradient(size / 2, size / 2, 0, size / 2, size / 2, size / 2);
    gradient.addColorStop(0, 'rgba(255,240,214,0.95)');
    gradient.addColorStop(0.35, 'rgba(238,216,182,0.45)');
    gradient.addColorStop(1, 'rgba(220,190,150,0)');
    context.fillStyle = gradient;
    context.fillRect(0, 0, size, size);

    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    return texture;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         SKY
//------------------------------------------------------------------------------------------------------------------------

// Equirectangular golden-hour gradient: dusty blue crown, warm haze band, pale floor.
export function SkyTexture(width = 2048, height = 1024)
{
    const canvas = document.createElement('canvas');
    canvas.width = width;
    canvas.height = height;
    const context = canvas.getContext('2d');

    const gradient = context.createLinearGradient(0, 0, 0, height);
    gradient.addColorStop(0.00, '#4f6390');
    gradient.addColorStop(0.18, '#7c86a6');
    gradient.addColorStop(0.34, '#b3a8a6');
    gradient.addColorStop(0.46, '#e4bf94');
    gradient.addColorStop(0.52, '#f7d8a8');
    gradient.addColorStop(0.58, '#e6c39a');
    gradient.addColorStop(0.72, '#c2a37e');
    gradient.addColorStop(1.00, '#9c8163');
    context.fillStyle = gradient;
    context.fillRect(0, 0, width, height);

    // Warm scatter just above the horizon line.
    const band = context.createLinearGradient(0, height * 0.40, 0, height * 0.56);
    band.addColorStop(0, 'rgba(255,206,150,0)');
    band.addColorStop(0.5, 'rgba(255,214,166,0.55)');
    band.addColorStop(1, 'rgba(255,196,144,0)');
    context.fillStyle = band;
    context.fillRect(0, 0, width, height);

    // Thin high cirrus, stretched horizontally.
    const random = Random(6161);
    context.lineWidth = 2;
    for (let i = 0; i < 90; i++)
    {
        const y = height * (0.10 + random() * 0.30);
        const x = random() * width;
        const length = 120 + random() * 460;
        context.strokeStyle = `rgba(255,238,220,${0.03 + random() * 0.07})`;
        context.beginPath();
        context.moveTo(x, y);
        context.bezierCurveTo(x + length * 0.3, y - 8, x + length * 0.7, y + 6, x + length, y - 2);
        context.stroke();
    }

    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.ClampToEdgeWrapping;
    return texture;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LIGHT SHAFT
//------------------------------------------------------------------------------------------------------------------------

// Soft vertical beam used for sun slots cutting into the tunnels.
export function BeamTexture(size = 128)
{
    const canvas = MakeCanvas(size);
    const context = canvas.getContext('2d');
    const horizontal = context.createLinearGradient(0, 0, size, 0);
    horizontal.addColorStop(0, 'rgba(255,236,204,0)');
    horizontal.addColorStop(0.5, 'rgba(255,236,204,0.85)');
    horizontal.addColorStop(1, 'rgba(255,236,204,0)');
    context.fillStyle = horizontal;
    context.fillRect(0, 0, size, size);

    const vertical = context.createLinearGradient(0, 0, 0, size);
    vertical.addColorStop(0, 'rgba(0,0,0,0)');
    vertical.addColorStop(0.25, 'rgba(0,0,0,0.15)');
    vertical.addColorStop(1, 'rgba(0,0,0,0.9)');
    context.globalCompositeOperation = 'destination-out';
    context.fillStyle = vertical;
    context.fillRect(0, 0, size, size);

    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    return texture;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SIGNAGE
//------------------------------------------------------------------------------------------------------------------------

// Stencilled maintenance boards mounted on the aqueduct walls.
export function SignTexture(Title, Subtitle, Accent = PALETTE.paintRed)
{
    const canvas = document.createElement('canvas');
    canvas.width = 512;
    canvas.height = 256;
    const context = canvas.getContext('2d');
    const random = Random(Title.length * 977 + 13);

    context.fillStyle = '#ded6c3';
    context.fillRect(0, 0, 512, 256);

    context.strokeStyle = Accent;
    context.lineWidth = 10;
    context.strokeRect(16, 16, 480, 224);

    for (let i = 0; i < 1400; i++)
    {
        context.fillStyle = `rgba(120,108,92,${random() * 0.06})`;
        context.fillRect(random() * 512, random() * 256, 3, 3);
    }

    context.fillStyle = Accent;
    context.font = 'bold 92px Impact, "Arial Narrow", "Haettenschweiler", sans-serif';
    context.textAlign = 'center';
    context.textBaseline = 'middle';
    context.fillText(Title, 256, 104);

    context.fillStyle = '#3c4247';
    context.font = 'bold 34px "Arial Narrow", "Helvetica Neue", sans-serif';
    context.fillText(Subtitle, 256, 186);

    // Chipped paint.
    for (let i = 0; i < 90; i++)
    {
        context.fillStyle = `rgba(222,214,195,${0.2 + random() * 0.5})`;
        context.fillRect(random() * 512, random() * 256, 2 + random() * 10, 2 + random() * 4);
    }

    const texture = new THREE.CanvasTexture(canvas);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.anisotropy = 4;
    return texture;
}
