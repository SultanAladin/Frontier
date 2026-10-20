import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
const Read=File=>fs.readFileSync(new URL('../Shader/'+File,import.meta.url),'utf8');
test('screen GI history excludes view-dependent direct specular peaks',()=>{
 const Resolve=Read('DisplayRecombination.wgsl');
 assert.match(Resolve,/textureLoad\(DiffuseDirectRadiance, Pixel, 0\)\.rgb \+ Diffuse \+ Emission/);
 assert.doesNotMatch(Resolve,/min\(Direct\.rgb \+ Diffuse \+ Emission/);
 const Direct=Read('DirectRadiance.wgsl');
 assert.match(Direct,/DiffuseAlbedo \* DiffuseArrival/);
});
test('a screen intersection without valid radiance coverage falls through to the world tracer',()=>{
 for(const File of ['ReservoirTemporal.wgsl','ReflectionTraversal.wgsl']){
  const Source=Read(File);
  assert.match(Source,/if \(Strike\.Hit && ScreenExitance\.w > 0\.0\)/);
  assert.match(Source,/else if \(UseR[at]/);
 }
});
