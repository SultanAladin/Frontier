// Candy materials.
//
// Everything is physically based and sweet-shop specific: glossy poured icing,
// translucent gummy taffy, crystalline rock candy, and a sugar-dusted fondant
// hide. Procedural canvas textures keep the scene asset-free and instant.

import * as THREE from 'three';

const HAS_DOM = typeof document !== 'undefined';

/** Canvas-backed procedural texture helper (browser only). */
function canvasTexture(size, draw, repeat = [1, 1]) {
  if (!HAS_DOM) return null;
  const c = document.createElement('canvas');
  c.width = c.height = size;
  const ctx = c.getContext('2d');
  draw(ctx, size);
  const tex = new THREE.CanvasTexture(c);
  tex.wrapS = tex.wrapT = THREE.RepeatWrapping;
  tex.repeat.set(repeat[0], repeat[1]);
  tex.colorSpace = THREE.SRGBColorSpace;
  tex.anisotropy = 4;
  return tex;
}

/** Sugar grain: fine speckle that breaks up the gloss. */
function grainTexture() {
  return canvasTexture(256, (ctx, s) => {
    ctx.fillStyle = '#ffffff';
    ctx.fillRect(0, 0, s, s);
    for (let i = 0; i < 9000; i++) {
      const x = Math.random() * s;
      const y = Math.random() * s;
      const r = Math.random() * 1.5 + 0.3;
      const v = 205 + Math.random() * 50;
      ctx.fillStyle = `rgba(${v},${v * 0.95},${v},${0.5 + Math.random() * 0.4})`;
      ctx.beginPath();
      ctx.arc(x, y, r, 0, Math.PI * 2);
      ctx.fill();
    }
    // Faint sugar veins
    ctx.strokeStyle = 'rgba(255,255,255,0.35)';
    for (let i = 0; i < 40; i++) {
      ctx.lineWidth = Math.random() * 2 + 0.4;
      ctx.beginPath();
      const y = Math.random() * s;
      ctx.moveTo(0, y);
      for (let x = 0; x < s; x += 16) ctx.lineTo(x, y + Math.sin(x * 0.05 + i) * 4);
      ctx.stroke();
    }
  }, [4, 4]);
}

/** Candy-cane stripe: diagonal bands for wing bones and horns. */
function caneStripe(a, b) {
  return canvasTexture(256, (ctx, s) => {
    ctx.fillStyle = a;
    ctx.fillRect(0, 0, s, s);
    ctx.fillStyle = b;
    for (let i = -s; i < s * 2; i += 46) {
      ctx.save();
      ctx.translate(i, 0);
      ctx.rotate(0.42);
      ctx.fillRect(0, -s, 20, s * 3);
      ctx.restore();
    }
  }, [3, 1]);
}

/** Rock-candy swirl: layered pastel bands for the wings and belly. */
function taffySwirl(baseA, baseB) {
  return canvasTexture(512, (ctx, s) => {
    ctx.fillStyle = baseA;
    ctx.fillRect(0, 0, s, s);
    ctx.save();
    ctx.translate(s / 2, s / 2);
    for (let i = 0; i < 26; i++) {
      ctx.rotate(0.12);
      ctx.strokeStyle = i % 2 ? baseB : baseA;
      ctx.globalAlpha = 0.75;
      ctx.lineWidth = 8 + Math.random() * 22;
      ctx.beginPath();
      for (let a = 0; a < Math.PI * 2; a += 0.1) {
        const r = s * 0.06 + a * s * 0.045 + i * 3;
        const x = Math.cos(a) * r;
        const y = Math.sin(a) * r * 0.8;
        if (a === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
      }
      ctx.stroke();
    }
    ctx.restore();
  }, [1, 1]);
}

export function createCandyMaterials(opts = {}) {
  const sugar = grainTexture();
  const wingTex = taffySwirl('#7a2ce0', '#c58cff');
  const caneTex = caneStripe('#ff6fa8', '#fff4fa');
  const hornTex = caneStripe('#ffb03a', '#fff0c9');

  const common = {
    clearcoat: 1,
    clearcoatRoughness: 0.12,
    envMapIntensity: 1.25,
  };

  const mats = {
    // body: raspberry fondant with a sugar crust
    body: new THREE.MeshPhysicalMaterial({
      color: 0xffffff,
      vertexColors: true,
      roughness: 0.42,
      metalness: 0,
      clearcoat: 0.85,
      clearcoatRoughness: 0.22,
      sheen: 0.85,
      sheenColor: new THREE.Color(0xffb3d9),
      sheenRoughness: 0.6,
      bumpMap: sugar || null,
      bumpScale: 0.06,
      ...common,
    }),
    // belly: pale strawberry cream
    belly: new THREE.MeshPhysicalMaterial({
      color: 0xffc7dd,
      roughness: 0.5,
      clearcoat: 0.7,
      sheen: 0.9,
      sheenColor: new THREE.Color(0xffe3f0),
      envMapIntensity: 1.1,
    }),
    head: new THREE.MeshPhysicalMaterial({
      color: 0xe04a84,
      roughness: 0.34,
      clearcoat: 1,
      clearcoatRoughness: 0.14,
      sheen: 0.7,
      sheenColor: new THREE.Color(0xffc0dc),
      bumpMap: sugar || null,
      bumpScale: 0.05,
      ...common,
    }),
    jaw: new THREE.MeshPhysicalMaterial({
      color: 0xc93468,
      roughness: 0.3,
      clearcoat: 1,
      clearcoatRoughness: 0.12,
      ...common,
    }),
    mouth: new THREE.MeshStandardMaterial({
      color: 0x5d0e2c,
      roughness: 0.55,
      emissive: 0x2a0410,
      emissiveIntensity: 0.6,
    }),
    tongue: new THREE.MeshPhysicalMaterial({
      color: 0xff6f9f,
      roughness: 0.22,
      clearcoat: 1,
      transmission: 0.25,
      thickness: 0.4,
    }),
    tooth: new THREE.MeshPhysicalMaterial({
      color: 0xfff6fb,
      roughness: 0.07,
      metalness: 0,
      clearcoat: 1,
      clearcoatRoughness: 0.03,
      transmission: 0.35,
      thickness: 0.35,
      ior: 1.6,
      envMapIntensity: 1.6,
    }),
    // horns: butterscotch candy cane
    horn: new THREE.MeshPhysicalMaterial({
      color: 0xffffff,
      map: hornTex || null,
      userData: { tint: 0xffb457 },
      roughness: 0.18,
      clearcoat: 1,
      clearcoatRoughness: 0.06,
      transmission: 0.2,
      thickness: 0.6,
      ior: 1.5,
      emissive: 0x3a1a00,
      emissiveIntensity: 0.25,
      envMapIntensity: 1.5,
    }),
    // crystal spikes: mint rock candy
    spike: new THREE.MeshPhysicalMaterial({
      color: 0x4fe6a0,
      roughness: 0.06,
      metalness: 0.05,
      clearcoat: 1,
      transmission: 0.55,
      thickness: 1.1,
      ior: 1.7,
      iridescence: 0.7,
      iridescenceIOR: 1.4,
      envMapIntensity: 1.8,
      flatShading: true,
    }),
    wing: new THREE.MeshPhysicalMaterial({
      color: 0x8a3cf0,
      map: wingTex || null,
      userData: { tint: 0x8a3cf0 },
      roughness: 0.28,
      clearcoat: 1,
      clearcoatRoughness: 0.1,
      sheen: 1,
      sheenColor: new THREE.Color(0xd9a6ff),
      transmission: 0.22,
      thickness: 0.5,
      side: THREE.DoubleSide,
      iridescence: 0.55,
      iridescenceIOR: 1.35,
      envMapIntensity: 1.35,
    }),
    bone: new THREE.MeshPhysicalMaterial({
      color: 0xffffff,
      map: caneTex || null,
      userData: { tint: 0xff77ad },
      roughness: 0.22,
      clearcoat: 1,
      clearcoatRoughness: 0.08,
      transmission: 0.15,
      thickness: 0.5,
      envMapIntensity: 1.4,
    }),
    leg: new THREE.MeshPhysicalMaterial({
      color: 0xffffff,
      vertexColors: true,
      roughness: 0.4,
      clearcoat: 0.9,
      clearcoatRoughness: 0.18,
      sheen: 0.8,
      sheenColor: new THREE.Color(0xffb7d6),
      bumpMap: sugar || null,
      bumpScale: 0.05,
    }),
    // Claws: hard candy, tinted so they don't blow out to white.
    claw: new THREE.MeshPhysicalMaterial({
      color: 0xffd9ea,
      roughness: 0.16,
      clearcoat: 1,
      clearcoatRoughness: 0.06,
      transmission: 0.18,
      thickness: 0.4,
      ior: 1.55,
      sheen: 0.6,
      sheenColor: new THREE.Color(0xffb6d6),
      envMapIntensity: 1.2,
    }),
    // Instance colours (setColorAt) drive the jimmies, NOT per-vertex colours:
    // declaring vertexColors here would force a missing colour attribute.
    sprinkle: new THREE.MeshPhysicalMaterial({
      color: 0xffffff,
      roughness: 0.22,
      clearcoat: 1,
      clearcoatRoughness: 0.07,
      sheen: 0.5,
      sheenColor: new THREE.Color(0xffffff),
      envMapIntensity: 1.5,
    }),
    gumdrop: new THREE.MeshPhysicalMaterial({
      color: 0xff7ab6,
      roughness: 0.14,
      clearcoat: 1,
      transmission: 0.3,
      thickness: 0.7,
      ior: 1.45,
      sheen: 0.8,
      sheenColor: new THREE.Color(0xffd6ea),
      envMapIntensity: 1.5,
    }),
    eye: new THREE.MeshPhysicalMaterial({
      color: 0xffb02e,
      emissive: 0xff5a00,
      emissiveIntensity: 2.4,
      roughness: 0.05,
      clearcoat: 1,
      metalness: 0.1,
      envMapIntensity: 2,
    }),
    pupil: new THREE.MeshStandardMaterial({
      color: 0x14040a,
      roughness: 0.15,
      metalness: 0.2,
    }),
    heart: new THREE.MeshPhysicalMaterial({
      color: 0xff3d8a,
      emissive: 0xff2a72,
      emissiveIntensity: 1.6,
      roughness: 0.1,
      clearcoat: 1,
      transmission: 0.6,
      thickness: 1.2,
    }),
  };

  mats.textures = { sugar, wingTex, caneTex, hornTex };
  return mats;
}
