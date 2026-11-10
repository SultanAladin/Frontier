// Road Editor — world road-type catalog.
// Every entry is a distinct cross-section + surface + furniture recipe so the
// generated city never looks uniform. Lanes are ordered left → right.
// dir: +1 forward, -1 opposing, 0 non-directional.

export const LANE_KINDS = {
  drive:   { name: 'Driving lane' },
  bus:     { name: 'Bus lane' },
  bike:    { name: 'Cycle lane' },
  parking: { name: 'Parking lane' },
  median:  { name: 'Median' },
  shoulder:{ name: 'Shoulder' },
  tram:    { name: 'Tram lane' },
  walk:    { name: 'Footway' },
  verge:   { name: 'Verge' },
  rail:    { name: 'Rail' },
};

// curb: barrier | rolled | mountable | stone | none
// surface: asphalt | asphalt-dark | concrete | cobble | sett | gravel | dirt | grass | paver | red-bus | brick

function L(kind, dir, width, extra = {}) { return { kind, dir, width, ...extra }; }

export const ROAD_TYPES = [
  { id: 'motorway-6', name: 'Motorway 6-lane', category: 'Motorway', speed: 120, surface: 'asphalt-dark',
    lanes: [L('shoulder',0,2.5),L('drive',1,3.6),L('drive',1,3.6),L('drive',1,3.6),L('median',0,4),L('drive',-1,3.6),L('drive',-1,3.6),L('drive',-1,3.6),L('shoulder',0,2.5)],
    curb: 'barrier', sidewalk: 'none', center: 'barrier', edgeLine: 'solid', laneLine: 'dashed-white', guardrail: true, lights: 'motorway' },
  { id: 'motorway-4', name: 'Motorway 4-lane', category: 'Motorway', speed: 120, surface: 'asphalt-dark',
    lanes: [L('shoulder',0,2.5),L('drive',1,3.6),L('drive',1,3.6),L('median',0,3),L('drive',-1,3.6),L('drive',-1,3.6),L('shoulder',0,2.5)],
    curb: 'barrier', sidewalk: 'none', center: 'barrier', edgeLine: 'solid', laneLine: 'dashed-white', guardrail: true, lights: 'motorway' },
  { id: 'freeway-ramp-1', name: 'Highway ramp (1 lane)', category: 'Motorway', speed: 60, surface: 'asphalt',
    lanes: [L('shoulder',0,1.5),L('drive',1,4.2),L('shoulder',0,1.5)],
    curb: 'barrier', sidewalk: 'none', center: 'none', edgeLine: 'solid', laneLine: 'none', guardrail: true },
  { id: 'freeway-ramp-2', name: 'Highway ramp (2 lane)', category: 'Motorway', speed: 60, surface: 'asphalt',
    lanes: [L('shoulder',0,2),L('drive',1,3.6),L('drive',1,3.6),L('shoulder',0,2)],
    curb: 'barrier', sidewalk: 'none', center: 'none', edgeLine: 'solid', laneLine: 'dashed-white', guardrail: true },
  { id: 'arterial-6', name: 'Arterial boulevard 6-lane', category: 'Arterial', speed: 60, surface: 'asphalt',
    lanes: [L('bike',1,1.8),L('bus',1,3.2),L('drive',1,3.2),L('drive',1,3.2),L('median',0,2.5,{planted:true}),L('drive',-1,3.2),L('drive',-1,3.2),L('bus',-1,3.2),L('bike',-1,1.8)],
    curb: 'barrier', sidewalk: 'paver-3m', center: 'median', edgeLine: 'solid', laneLine: 'dashed-white', lights: 'avenue', trees: true },
  { id: 'arterial-4', name: 'Arterial 4-lane + turn lane', category: 'Arterial', speed: 60, surface: 'asphalt',
    lanes: [L('bike',1,1.6),L('drive',1,3.3),L('drive',1,3.3),L('drive',0,3.3,{turn:true}),L('drive',-1,3.3),L('drive',-1,3.3),L('bike',-1,1.6)],
    curb: 'rolled', sidewalk: 'concrete-2m', center: 'turn-lane', edgeLine: 'solid', laneLine: 'dashed-white', lights: 'avenue' },
  { id: 'arterial-2', name: 'Arterial 2-lane', category: 'Arterial', speed: 50, surface: 'asphalt',
    lanes: [L('bike',1,1.6),L('drive',1,3.4),L('drive',-1,3.4),L('bike',-1,1.6)],
    curb: 'rolled', sidewalk: 'concrete-2m', center: 'double-yellow', edgeLine: 'solid', laneLine: 'none', lights: 'street' },
  { id: 'collector-2', name: 'Collector + parking', category: 'Urban', speed: 40, surface: 'asphalt',
    lanes: [L('parking',0,2.2),L('bike',1,1.5),L('drive',1,3.1),L('drive',-1,3.1),L('bike',-1,1.5),L('parking',0,2.2)],
    curb: 'rolled', sidewalk: 'concrete-2m', center: 'dashed-yellow', edgeLine: 'none', laneLine: 'dashed-white', lights: 'street' },
  { id: 'local-2', name: 'Local street', category: 'Urban', speed: 30, surface: 'asphalt',
    lanes: [L('parking',0,2),L('drive',1,3),L('drive',-1,3),L('parking',0,2)],
    curb: 'rolled', sidewalk: 'concrete-1.5m', center: 'dashed-white', edgeLine: 'none', laneLine: 'none', lights: 'street', bumps: 'table' },
  { id: 'oneway-3', name: 'One-way 3-lane', category: 'Urban', speed: 40, surface: 'asphalt',
    lanes: [L('parking',0,2),L('drive',1,3.1),L('drive',1,3.1),L('drive',1,3.1),L('bike',1,1.5)],
    curb: 'rolled', sidewalk: 'concrete-2m', center: 'none', edgeLine: 'none', laneLine: 'dashed-white', oneway: true },
  { id: 'oneway-2-contra', name: 'One-way + contraflow cycle', category: 'Urban', speed: 30, surface: 'asphalt',
    lanes: [L('bike',-1,1.7,{contra:true}),L('drive',1,3.2),L('drive',1,3.2),L('parking',0,2)],
    curb: 'rolled', sidewalk: 'concrete-2m', center: 'solid-white', edgeLine: 'none', laneLine: 'dashed-white', oneway: true },
  { id: 'alley-1', name: 'Rear alley', category: 'Urban', speed: 15, surface: 'concrete',
    lanes: [L('drive',0,3.4)],
    curb: 'none', sidewalk: 'none', center: 'none', edgeLine: 'none', laneLine: 'none' },
  { id: 'historic-cobble', name: 'Historic cobblestone', category: 'Urban', speed: 20, surface: 'cobble',
    lanes: [L('drive',1,2.8),L('drive',-1,2.8)],
    curb: 'stone', sidewalk: 'sett-2m', center: 'none', edgeLine: 'none', laneLine: 'none', lights: 'heritage', bollards: 'cast-iron' },
  { id: 'brick-street', name: 'Brick street', category: 'Urban', speed: 30, surface: 'brick',
    lanes: [L('parking',0,2),L('drive',1,3),L('drive',-1,3),L('parking',0,2)],
    curb: 'stone', sidewalk: 'brick-2m', center: 'none', edgeLine: 'none', laneLine: 'none', lights: 'heritage' },
  { id: 'busway-2', name: 'Busway corridor', category: 'Transit', speed: 50, surface: 'red-bus',
    lanes: [L('bus',1,3.4),L('bus',-1,3.4)],
    curb: 'rolled', sidewalk: 'concrete-2m', center: 'solid-white', edgeLine: 'bus-line', laneLine: 'none', busStop: true },
  { id: 'tram-street', name: 'Tram street', category: 'Transit', speed: 40, surface: 'sett',
    lanes: [L('drive',1,2.6),L('tram',1,2.4,{rails:true}),L('tram',-1,2.4,{rails:true}),L('drive',-1,2.6)],
    curb: 'mountable', sidewalk: 'paver-3m', center: 'rails', edgeLine: 'none', laneLine: 'none', catenary: true },
  { id: 'promenade', name: 'Promenade (pedestrian)', category: 'Path', speed: 10, surface: 'paver',
    lanes: [L('walk',0,3),L('bike',1,1.6),L('bike',-1,1.6),L('walk',0,3)],
    curb: 'none', sidewalk: 'itself', center: 'paver-band', edgeLine: 'none', laneLine: 'none', bollards: 'steel', lights: 'plaza' },
  { id: 'cycle-track', name: 'Cycle superhighway', category: 'Path', speed: 30, surface: 'asphalt-green',
    lanes: [L('bike',1,2),L('bike',-1,2),L('walk',0,2)],
    curb: 'none', sidewalk: 'itself', center: 'dashed-white', edgeLine: 'solid-green', laneLine: 'none' },
  { id: 'footpath', name: 'Footpath', category: 'Path', speed: 5, surface: 'gravel-fine',
    lanes: [L('walk',0,2.4)],
    curb: 'none', sidewalk: 'itself', center: 'none', edgeLine: 'none', laneLine: 'none' },
  { id: 'trail-gravel', name: 'Gravel trail', category: 'Path', speed: 15, surface: 'gravel',
    lanes: [L('drive',0,2.6)],
    curb: 'none', sidewalk: 'none', center: 'none', edgeLine: 'none', laneLine: 'none' },
  { id: 'rural-highway', name: 'Rural highway', category: 'Rural', speed: 90, surface: 'asphalt',
    lanes: [L('shoulder',0,1.8),L('drive',1,3.5),L('drive',-1,3.5),L('shoulder',0,1.8)],
    curb: 'none', sidewalk: 'none', center: 'double-yellow', edgeLine: 'rumble', laneLine: 'none', guardrail: 'posts' },
  { id: 'rural-lane', name: 'Country lane', category: 'Rural', speed: 60, surface: 'asphalt-worn',
    lanes: [L('drive',1,2.7),L('drive',-1,2.7)],
    curb: 'none', sidewalk: 'none', center: 'none', edgeLine: 'none', laneLine: 'none', hedges: true },
  { id: 'gravel-road', name: 'Gravel road', category: 'Rural', speed: 50, surface: 'gravel',
    lanes: [L('drive',1,2.8),L('drive',-1,2.8)],
    curb: 'none', sidewalk: 'none', center: 'none', edgeLine: 'none', laneLine: 'none' },
  { id: 'dirt-track', name: 'Dirt track (offroad)', category: 'Offroad', speed: 30, surface: 'dirt',
    lanes: [L('drive',0,3.6,{ruts:true})],
    curb: 'none', sidewalk: 'none', center: 'none', edgeLine: 'none', laneLine: 'none' },
  { id: 'farm-track', name: 'Farm track (grass crown)', category: 'Offroad', speed: 20, surface: 'dirt-grass',
    lanes: [L('drive',0,3.2,{grassCrown:true})],
    curb: 'none', sidewalk: 'none', center: 'grass', edgeLine: 'none', laneLine: 'none' },
  { id: 'forest-trail', name: 'Forest singletrack', category: 'Offroad', speed: 15, surface: 'dirt-forest',
    lanes: [L('drive',0,1.8)],
    curb: 'none', sidewalk: 'none', center: 'none', edgeLine: 'none', laneLine: 'none' },
  { id: 'mountain-pass', name: 'Mountain pass', category: 'Rural', speed: 40, surface: 'asphalt',
    lanes: [L('drive',1,3.1),L('drive',-1,3.1)],
    curb: 'stone-wall', sidewalk: 'none', center: 'double-yellow', edgeLine: 'solid', laneLine: 'none', guardrail: 'stone', rockCut: true },
  { id: 'causeway', name: 'Causeway / low bridge', category: 'Special', speed: 60, surface: 'concrete',
    lanes: [L('shoulder',0,1.5),L('drive',1,3.4),L('drive',-1,3.4),L('shoulder',0,1.5)],
    curb: 'parapet', sidewalk: 'walk-1.5m', center: 'double-yellow', edgeLine: 'solid', laneLine: 'none', water: true },
  { id: 'service-road', name: 'Frontage / service road', category: 'Urban', speed: 30, surface: 'asphalt',
    lanes: [L('drive',1,3),L('drive',-1,3)],
    curb: 'rolled', sidewalk: 'concrete-1.5m', center: 'dashed-white', edgeLine: 'none', laneLine: 'none' },
  { id: 'roundabout-ring', name: 'Roundabout ring', category: 'Special', speed: 25, surface: 'asphalt',
    lanes: [L('drive',0,7.5,{ring:true})],
    curb: 'rolled', sidewalk: 'none', center: 'island', edgeLine: 'solid', laneLine: 'none', roundabout: true },
  { id: 'rail-crossing', name: 'Rail crossing approach', category: 'Special', speed: 40, surface: 'asphalt',
    lanes: [L('drive',1,3.2),L('drive',-1,3.2)],
    curb: 'rolled', sidewalk: 'concrete-1.5m', center: 'double-yellow', edgeLine: 'solid', laneLine: 'none', railCrossing: true },
  { id: 'tunnel-road', name: 'Sunken / tunnel approach', category: 'Special', speed: 60, surface: 'concrete-dark',
    lanes: [L('drive',1,3.4),L('drive',1,3.4),L('median',0,1.2,{wall:true}),L('drive',-1,3.4),L('drive',-1,3.4)],
    curb: 'barrier', sidewalk: 'none', center: 'wall', edgeLine: 'solid', laneLine: 'dashed-white', walls: true, lights: 'tunnel' },
  { id: 'boulevard-trees', name: 'Tree boulevard', category: 'Urban', speed: 40, surface: 'asphalt',
    lanes: [L('parking',0,2.1),L('bike',1,1.6),L('drive',1,3),L('drive',1,3),L('median',0,3.5,{trees:true}),L('drive',-1,3),L('drive',-1,3),L('bike',-1,1.6),L('parking',0,2.1)],
    curb: 'stone', sidewalk: 'paver-2.5m', center: 'tree-median', edgeLine: 'none', laneLine: 'dashed-white', lights: 'avenue', trees: true },
];

export const ROAD_CATEGORIES = ['Motorway', 'Arterial', 'Urban', 'Transit', 'Path', 'Rural', 'Offroad', 'Special'];

export function roadTypeById(id) {
  return ROAD_TYPES.find((t) => t.id === id) || ROAD_TYPES[7];
}

export function carriagewayWidth(type) {
  return type.lanes.reduce((a, l) => a + l.width, 0);
}

export function fullWidth(type) {
  let w = carriagewayWidth(type);
  const sw = sidewalkWidth(type.sidewalk);
  if (type.sidewalk !== 'none' && type.sidewalk !== 'itself') w += sw * 2;
  return w;
}

export function sidewalkWidth(spec) {
  if (!spec || spec === 'none' || spec === 'itself') return 0;
  const m = spec.match(/([\d.]+)m/);
  return m ? parseFloat(m[1]) : 2;
}

// Seeded per-road surface variation so no two roads look identical.
export function variantFor(edgeId, seed = 1) {
  let h = 2166136261 ^ seed;
  const s = String(edgeId);
  for (let i = 0; i < s.length; i++) { h ^= s.charCodeAt(i); h = Math.imul(h, 16777619); }
  const r = (n) => {
    h ^= h + n * 0x9E3779B9; h = Math.imul(h ^ (h >>> 15), 0x85EBCA6B);
    return ((h ^ (h >>> 13)) >>> 0) / 4294967296;
  };
  return {
    hueJitter: (r(1) - 0.5) * 0.05,
    wear: r(2),            // 0 fresh … 1 worn
    patches: Math.floor(r(3) * 5),
    dashPhase: r(4) * 6,
    skidTint: r(5),
  };
}
