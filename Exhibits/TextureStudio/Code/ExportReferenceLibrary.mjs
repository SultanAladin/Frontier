/*==========================================================================================================================================
                                                       EXPORTREFERENCELIBRARY.MJS
==========================================================================================================================================*/
// 🧩 Retain the HTML catalogue order, complete normalized recipes and the physical coefficients supported by native material evaluation.
import Fs from 'node:fs';
import Path from 'node:path';
import Crypto from 'node:crypto';
import {materials, normalizeMaterial} from '../../../Experimental/TextureStudio/src/materials.js';
import {getRecipe, recipeControlValue} from '../../../Experimental/TextureStudio/src/materialProfiles.js';
const Destination = Path.resolve('EngineContent/MaterialArchives/TextureStudio');
Fs.mkdirSync(Destination,{recursive:true});
const EncodeReferenceText = Text => JSON.stringify(String(Text));
const EncodeTomlTerm = Term => Array.isArray(Term) ? '[' + Term.map(EncodeTomlTerm).join(', ') + ']' :
    Term && typeof Term === 'object' ? '{' + Object.entries(Term).map(([Key, Coefficient])=>EncodeReferenceText(Key)+' = '+EncodeTomlTerm(Coefficient)).join(', ') + '}' : JSON.stringify(Term);
const ConstrainRecipeCoefficient = (Coefficient, Minimum, Maximum) => Math.max(Minimum,Math.min(Maximum,Number(Coefficient)||0));
const DecodeReferenceSpectrum = Hex => [1,3,5].map(Offset=>{const Channel=parseInt(Hex.slice(Offset,Offset+2),16)/255;return Channel<=.04045?Channel/12.92:((Channel+.055)/1.055)**2.4;});
let Catalogue = '# Generated from the HTML library; regenerate with ExportReferenceLibrary.mjs.\nFormat = 1\n';
for (const [Ordinal, Source] of materials.entries())
{
    const Recipe = normalizeMaterial(Source), Profile = getRecipe(Recipe);
    const Filename = `${Source.id}.material`;
    const Spectrum = DecodeReferenceSpectrum(Recipe.color||'#808080');
    let Material = `Format = 1\nName = ${EncodeReferenceText(Source.name)}\n\n[[Strata]]\nCategory = "Opaque"\nAlbedo = [${Spectrum.join(', ')}]\nRoughness = ${ConstrainRecipeCoefficient(Recipe.roughness,.045,1)}\nMetallic = ${ConstrainRecipeCoefficient(Recipe.metalness,0,1)}\nIor = ${ConstrainRecipeCoefficient(Recipe.ior||1.5,1,3)}\nCoverage = 1.0\nAnisotropy = ${ConstrainRecipeCoefficient(Recipe.anisotropy,-.9,.9)}\n`;
    if (Recipe.coat>0)
        Material += `\n[[Strata]]\nCategory = "Coating"\nAlbedo = [1.0, 1.0, 1.0]\nRoughness = ${ConstrainRecipeCoefficient(Recipe.coatRoughness,.045,1)}\nMetallic = 0.0\nIor = ${ConstrainRecipeCoefficient(Recipe.coatIor||1.5,1,3)}\nCoverage = ${ConstrainRecipeCoefficient(Recipe.coat,0,1)}\n`;
    if (Recipe.type === 0)
    {
        const Stops = Recipe.colors.map((Colour, Index) => [...DecodeReferenceSpectrum(Colour), Recipe.colorStops[Index]]);
        if (Recipe.colorMode === 'ramp') Stops.sort((First, Second) => First[3] - Second[3]);
        Material += `\n[AutomotivePaint]\nEnabled = true\nLayers = ${Recipe.flakeLayers}\nColourMode = "${Recipe.colorMode === 'ramp' ? 'Ramp' : Recipe.colorMode === 'single' ? 'Single' : 'Palette'}"\nDistribution = ${EncodeTomlTerm([Recipe.flakes, Recipe.flakeSize, Recipe.flakeScale, Recipe.depth])}\nFinish = ${EncodeTomlTerm([Recipe.flakeRoughnessMin, Recipe.flakeRoughnessMax, Recipe.flakeMetalnessMin, Recipe.flakeMetalnessMax])}\nRelief = ${EncodeTomlTerm([Recipe.flakeTilt, Recipe.flakeLayerDepth, Recipe.orangePeel, Recipe.orangePeelScale])}\nPalette = ${EncodeTomlTerm(Stops)}\n`;
    }
    Fs.writeFileSync(Path.join(Destination,Filename),Material);
    Catalogue += `\n[[Materials]]\nPath = ${EncodeReferenceText(Filename)}\nName = ${EncodeReferenceText(Source.name)}\nCategory = ${EncodeReferenceText(Source.category)}\nLabel = ${EncodeReferenceText(Source.label||'')}\nDescription = ${EncodeReferenceText(Source.description||'')}\nOrdinal = ${Ordinal}\nRecipeTitle = ${EncodeReferenceText(Profile.title)}\nRecipeCaption = ${EncodeReferenceText(Profile.caption)}\nColor = ${EncodeReferenceText(Recipe.color)}\nSourceRecipe = ${EncodeTomlTerm(Recipe)}\n`;
    for (const Control of Profile.controls)
    {
        Catalogue += `\n[[Materials.Controls]]\nId = ${EncodeReferenceText(Control.id)}\nCaption = ${EncodeReferenceText(Control.label)}\nGroup = ${EncodeReferenceText(Control.group)}\nPosition = ${recipeControlValue(Recipe,Control)}\nMinimum = ${Control.direct?Control.min:0}\nMaximum = ${Control.direct?Control.max:1}\nDirect = ${Boolean(Control.direct)}\nLogarithmic = ${Boolean(Control.log)}\nStep = ${Control.step || .01}\nUnit = ${EncodeReferenceText(Control.unit || "")}\n`;
        for (const Target of Control.targets || [{key:Control.key,min:Control.min,max:Control.max}])
            Catalogue += `\n[[Materials.Controls.Targets]]\nKey = ${EncodeReferenceText(Target.key)}\nMinimum = ${Target.min}\nMaximum = ${Target.max}\n`;
    }
}
Fs.writeFileSync(Path.join(Destination,'Library.toml'),Catalogue);
const Sources = ['materials.js','materialProfiles.js','catalogExpansion.js','architectureCatalog.js','granularCatalog.js','botanicalCatalog.js'].map(Name=>{const File='Experimental/TextureStudio/src/'+Name;return{file:File,sha256:Crypto.createHash('sha256').update(Fs.readFileSync(File)).digest('hex')};});
Fs.writeFileSync('Exhibits/TextureStudio/Results/ReferenceLibraryExport.json',JSON.stringify({count:materials.length,sources:Sources,outputs:Fs.readdirSync(Destination).sort().map(Name=>({file:'EngineContent/MaterialArchives/TextureStudio/'+Name,sha256:Crypto.createHash('sha256').update(Fs.readFileSync(Path.join(Destination,Name))).digest('hex')})),rendering_boundary:'Complete HTML normalized recipes retained; native physical coefficients and executable automotive paint parameters exported. Other procedural families are not yet ported.'},null,2)+'\n');
console.log(`Exported ${materials.length} HTML presets with complete normalized recipes and ordered metadata.`);
