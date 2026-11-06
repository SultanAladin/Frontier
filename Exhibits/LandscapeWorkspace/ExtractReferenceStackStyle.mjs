/*==========================================================================================================================================
                                                      EXTRACTREFERENCESTACKSTYLE.MJS
==========================================================================================================================================*/
// 🧩 Preserve upstream outliner/inspector CSS declarations and cascade, excluding unrelated workspace selectors.

import { readFile,writeFile } from 'node:fs/promises';
import { createRequire } from 'node:module';
const Require=createRequire(new URL('../../Experimental/LandscapeWorkspace/package.json',import.meta.url));
const PostCSS=Require('postcss');
const Source=await readFile(new URL('../../Experimental/LandscapeWorkspace/ReferenceSnapshot/style.css',import.meta.url),'utf8');
const Root=PostCSS.parse(Source);
const Prefixes=['tab-strip','document-tab','tab-close','tab-add','pane-scroll','pane-heading','pane-title','outliner','count-pill','add-','menu-','layer-','stat-','drag-','step-','icon-btn','mask-','locked-','lock-','empty','inspector-','identity-','eyebrow','io-','property-card','card-note','card-head','field-row','select','control-','amount','toggle','seed-'];
Root.walkRules(Rule=>
{
    const Selectors=Rule.selectors.filter(Selector=>Selector===':root'||Prefixes.some(Prefix=>Selector.includes('.tp-'+Prefix)));
    if(!Selectors.length)Rule.remove();else Rule.selectors=Selectors;
});
Root.walkComments(Comment=>Comment.remove());Root.walkAtRules(Rule=>{if(!Rule.nodes?.length)Rule.remove();});
const Header='/* Source: c7egoist/Frontier src/app/style.css @ 09b7064f282f4df5c5352f2bc97c82fb1c06a098.\n   Extracted declarations and cascade preserved; not an independently re-created skin. See ReferenceSnapshot/README.md. */\n';
await writeFile(new URL('../../Experimental/LandscapeWorkspace/ReferenceStack.css',import.meta.url),Header+Root.toString()+'\n');
