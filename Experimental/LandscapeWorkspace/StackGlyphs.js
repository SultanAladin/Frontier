/*==========================================================================================================================================
                                                              STACKGLYPHS.JS
==========================================================================================================================================*/
// 🧩 The reference's Lucide glyphs and operation colours for outliner rows, add menu, and inspector identity.

import { Mountain,MountainSnow,Layers,Columns3,MoveHorizontal,Rainbow,Sun,Lock,Eye,EyeOff,Contrast,Check,Wind,Search } from 'lucide';
const Glyphs={mountain:Mountain,ridge:MountainSnow,strata:Layers,stacks:Columns3,cliff:MoveHorizontal,cave:Rainbow,sun:Sun,lock:Lock,eye:Eye,'eye-off':EyeOff,mask:Contrast,check:Check,wind:Wind,search:Search};
export function StackGlyph(Token)
{
    const Size=['lock','eye','eye-off','mask','check'].includes(Token)?16:18;
    const SVG=document.createElementNS('http://www.w3.org/2000/svg','svg');
    for(const [Name,Setting] of Object.entries({viewBox:'0 0 24 24',width:Size,height:Size,fill:'none',stroke:'currentColor','stroke-width':2,'stroke-linecap':'round','stroke-linejoin':'round','aria-hidden':'true'}))SVG.setAttribute(Name,Setting);
    for(const [Tag,Attributes] of Glyphs[Token]||Mountain)
    {
        const Shape=document.createElementNS('http://www.w3.org/2000/svg',Tag);
        for(const [Name,Setting] of Object.entries(Attributes))Shape.setAttribute(Name,Setting);SVG.append(Shape);
    }
    return SVG;
}
export function StackAppearance(Type)
{
    if(Type==='environment')return {Glyph:'sun',Color:'#ffd166'};
    if(Type==='foundation')return {Glyph:'mountain',Color:'#f0b35e'};
    if(Type==='tunnel'||Type==='arch')return {Glyph:'cave',Color:'#9d7bff'};
    if(Type==='stacks')return {Glyph:'stacks',Color:'#d98cb3'};
    if(Type==='strata')return {Glyph:'strata',Color:'#e8d06a'};
    if(Type==='fault')return {Glyph:'cliff',Color:'#ff6b6b'};
    if(Type==='sand-dunes'||Type==='dunes')return {Glyph:'wind',Color:'#5eead4'};
    return {Glyph:['ridges','ridge-noise','fractal-ridges','ridged-multifractal'].includes(Type)?'ridge':'mountain',Color:'#ff8a5c'};
}
