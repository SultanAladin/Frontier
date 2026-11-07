/*====================================================================================================================================
                                                          WORLD.JS
====================================================================================================================================*/
// 🧩 Golden-hour environment: sky shell, sun, desert floor, distant mesas, reservoir, birds, airborne dust, and the shadow rig.

import * as THREE from '../vendor/three.module.min.js';
import { PALETTE, DustSprite } from './textures.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const SUN_DIRECTION = new THREE.Vector3(-0.42, 0.38, 0.82).normalize();
export const HAZE_COLOR = 0xe0c8a6;

//------------------------------------------------------------------------------------------------------------------------
//                                                       WORLD
//------------------------------------------------------------------------------------------------------------------------

export class World
{
    constructor(scene, textures)
    {
        this.scene = scene;
        this.time = 0;

        scene.fog = new THREE.FogExp2(HAZE_COLOR, 0.00092);
        scene.background = new THREE.Color(HAZE_COLOR);

        this.buildLights();
        this.buildSky(textures);
        this.buildGround(textures);
        this.buildDistant();
        this.buildBirds();
        this.buildMotes(textures);
    }

    //-------------------------------------------------------- Lighting -------------------------------------------------------------

    buildLights()
    {
        // Warm sky bounce against ochre concrete; the key stays low for long shadows.
        const hemisphere = new THREE.HemisphereLight(0xf6cf9c, 0x6b5a44, 1.0);
        this.scene.add(hemisphere);

        const ambient = new THREE.AmbientLight(0xc4ad92, 0.28);
        this.scene.add(ambient);

        const key = new THREE.DirectionalLight(PALETTE.sunLight, 3.1);
        key.position.copy(SUN_DIRECTION).multiplyScalar(160);
        key.castShadow = true;
        key.shadow.mapSize.set(2048, 2048);
        key.shadow.camera.near = 40;
        key.shadow.camera.far = 420;
        key.shadow.camera.left = -95;
        key.shadow.camera.right = 95;
        key.shadow.camera.top = 95;
        key.shadow.camera.bottom = -95;
        key.shadow.bias = -0.0005;
        key.shadow.normalBias = 0.6;
        this.scene.add(key);
        this.scene.add(key.target);
        this.key = key;

        // Cool rim from the opposite side keeps the concrete from going monochrome.
        const rim = new THREE.DirectionalLight(0x9fb3d4, 0.55);
        rim.position.copy(SUN_DIRECTION).multiplyScalar(-100).setY(45);
        this.scene.add(rim);
    }

    //-------------------------------------------------------- Sky shell ------------------------------------------------------------

    buildSky(textures)
    {
        const geometry = new THREE.SphereGeometry(5200, 48, 24);
        const material = new THREE.MeshBasicMaterial({
            map: textures.sky,
            side: THREE.BackSide,
            fog: false,
            depthWrite: false,
        });
        this.sky = new THREE.Mesh(geometry, material);
        this.sky.frustumCulled = false;
        this.scene.add(this.sky);

        const halo = new THREE.SpriteMaterial({
            map: textures.dust,
            color: 0xffd9a0,
            transparent: true,
            opacity: 0.55,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
            fog: false,
        });
        this.sunHalo = new THREE.Sprite(halo);
        this.sunHalo.scale.set(1500, 1500, 1);
        this.scene.add(this.sunHalo);

        const core = new THREE.SpriteMaterial({
            map: textures.dust,
            color: 0xfff3d6,
            transparent: true,
            opacity: 0.95,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
            fog: false,
        });
        this.sunCore = new THREE.Sprite(core);
        this.sunCore.scale.set(260, 260, 1);
        this.scene.add(this.sunCore);
    }

    //-------------------------------------------------------- Desert floor --------------------------------------------------------

    buildGround(textures)
    {
        const material = new THREE.MeshStandardMaterial({
            map: textures.desert,
            color: 0xd9bb92,
            roughness: 1.0,
            metalness: 0.0,
        });
        textureRepeat(textures.desert, 220);

        const floor = new THREE.Mesh(new THREE.PlaneGeometry(7000, 7000), material);
        floor.rotation.x = -Math.PI / 2;
        floor.position.y = -22;
        floor.receiveShadow = true;
        this.floor = floor;
        this.scene.add(floor);

        // Spillway apron directly under the aqueduct, catching the deck shadow.
        const apronMaterial = new THREE.MeshStandardMaterial({
            map: textures.desert,
            color: 0xb9a184,
            roughness: 1.0,
            metalness: 0.0,
        });
        const apron = new THREE.Mesh(new THREE.PlaneGeometry(1200, 1200), apronMaterial);
        apron.rotation.x = -Math.PI / 2;
        apron.position.y = -21.6;
        apron.receiveShadow = false;
        this.apron = apron;
        this.scene.add(apron);
    }

    //-------------------------------------------------------- Distance ------------------------------------------------------------

    buildDistant()
    {
        const group = new THREE.Group();
        group.name = 'distance';
        const random = mulberry(1993);

        const mesaMaterial = new THREE.MeshStandardMaterial({ color: 0xa8845f, roughness: 1, metalness: 0, flatShading: true });
        const farMesaMaterial = new THREE.MeshStandardMaterial({ color: 0xb59a80, roughness: 1, metalness: 0, flatShading: true });
        const concreteMaterial = new THREE.MeshStandardMaterial({ color: 0xc9bda8, roughness: 0.95, metalness: 0.02 });

        for (let i = 0; i < 22; i++)
        {
            const angle = random() * Math.PI * 2;
            const radius = 850 + random() * 900;
            const height = 55 + random() * 150;
            const width = 110 + random() * 320;
            const mesa = new THREE.Mesh(
                new THREE.CylinderGeometry(width * (0.55 + random() * 0.35), width, height, 7 + Math.floor(random() * 3)),
                radius > 1400 ? farMesaMaterial : mesaMaterial
            );
            mesa.position.set(Math.cos(angle) * radius, -22 + height / 2, Math.sin(angle) * radius);
            mesa.rotation.y = random() * Math.PI;
            mesa.receiveShadow = false;
            group.add(mesa);
        }

        // Terraced settlement blocks on the rim of the basin.
        for (let cluster = 0; cluster < 4; cluster++)
        {
            const angle = random() * Math.PI * 2;
            const radius = 430 + random() * 150;
            for (let block = 0; block < 7; block++)
            {
                const height = 12 + random() * 38;
                const box = new THREE.Mesh(
                    new THREE.BoxGeometry(18 + random() * 26, height, 18 + random() * 26),
                    concreteMaterial
                );
                const localAngle = angle + (random() - 0.5) * 0.12;
                const localRadius = radius + (random() - 0.5) * 90;
                box.position.set(Math.cos(localAngle) * localRadius, -22 + height / 2,(Math.sin(localAngle) * localRadius));
                box.rotation.y = random() * Math.PI;
                group.add(box);
            }
        }

        // A reservoir behind a dam wall on one flank: gives the map a horizon event.
        const waterMaterial = new THREE.MeshStandardMaterial({
            color: 0x5c9a92,
            roughness: 0.1,
            metalness: 0.4,
            transparent: true,
            opacity: 0.95,
        });
        const reservoir = new THREE.Mesh(new THREE.PlaneGeometry(2400, 1500), waterMaterial);
        reservoir.rotation.x = -Math.PI / 2;
        reservoir.position.set(1500, -20.4, 700);
        group.add(reservoir);

        const dam = new THREE.Mesh(new THREE.BoxGeometry(60, 26, 1600), concreteMaterial);
        dam.position.set(340, -22 + 13, 700);
        dam.rotation.y = 0.05;
        group.add(dam);

        // Far viaduct silhouette: repeated arches marching along the basin.
        const archGroup = new THREE.Group();
        for (let i = 0; i < 26; i++)
        {
            const pier = new THREE.Mesh(new THREE.BoxGeometry(4, 34, 5), concreteMaterial);
            pier.position.set(-700 + i * 44, -22 + 17, -820);
            archGroup.add(pier);
            if (i < 25)
            {
                const span = new THREE.Mesh(new THREE.BoxGeometry(46, 4, 8), concreteMaterial);
                span.position.set(-700 + i * 44 + 22, -22 + 34, -820);
                archGroup.add(span);
            }
        }
        group.add(archGroup);

        this.distant = group;
        this.scene.add(group);
    }

    //-------------------------------------------------------- Birds ---------------------------------------------------------------

    buildBirds()
    {
        this.birds = [];
        const material = new THREE.MeshBasicMaterial({ color: 0x3b332c, side: THREE.DoubleSide, fog: true });
        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.Float32BufferAttribute([
            0, 0, 0,
            1.1, 0.32, -0.35,
            1.0, 0.05, 0.45,
        ], 3));
        geometry.computeVertexNormals();

        for (let i = 0; i < 14; i++)
        {
            const bird = new THREE.Mesh(geometry, material);
            bird.scale.setScalar(1.6 + Math.random() * 1.6);
            this.birds.push({
                mesh     : bird,
                radius   : 90 + Math.random() * 190,
                height   : 45 + Math.random() * 55,
                speed    : 0.09 + Math.random() * 0.12,
                phase    : Math.random() * Math.PI * 2,
                flap     : 3 + Math.random() * 4,
            });
            this.scene.add(bird);
        }
        this.birdsOrigin = new THREE.Vector3();
        this.birdsClock = 0;
    }

    //-------------------------------------------------------- Dust motes -----------------------------------------------------------

    buildMotes(textures)
    {
        const count = 900;
        const positions = new Float32Array(count * 3);
        const sizes = new Float32Array(count);
        for (let i = 0; i < count; i++)
        {
            positions[i * 3 + 0] = (Math.random() - 0.5) * 150;
            positions[i * 3 + 1] = Math.random() * 46 - 6;
            positions[i * 3 + 2] = (Math.random() - 0.5) * 150;
            sizes[i] = 0.35 + Math.random() * 1.1;
        }

        const geometry = new THREE.BufferGeometry();
        geometry.setAttribute('position', new THREE.BufferAttribute(positions, 3));
        geometry.setAttribute('aSize', new THREE.BufferAttribute(sizes, 1));

        const material = new THREE.PointsMaterial({
            map: textures.dust,
            color: 0xffe6c0,
            size: 0.55,
            sizeAttenuation: true,
            transparent: true,
            opacity: 0.55,
            blending: THREE.AdditiveBlending,
            depthWrite: false,
            fog: true,
        });

        this.motes = new THREE.Points(geometry, material);
        this.motes.frustumCulled = false;
        this.scene.add(this.motes);
        this.motesOrigin = new THREE.Vector3();
    }

    //-------------------------------------------------------- Frame update ---------------------------------------------------------

    update(delta, camera, focus, speed)
    {
        this.time += delta;

        this.sky.position.copy(camera.position);
        this.sunHalo.position.copy(camera.position).addScaledVector(SUN_DIRECTION, 4200);
        this.sunCore.position.copy(camera.position).addScaledVector(SUN_DIRECTION, 4200);

        // Lighting rides with the rider so shadow-map density stays tight.
        this.key.position.set(focus.x + SUN_DIRECTION.x * 160, focus.y + SUN_DIRECTION.y * 160, focus.z + SUN_DIRECTION.z * 160);
        this.key.target.position.set(focus.x, focus.y, focus.z);
        this.key.target.updateMatrixWorld();

        this.floor.position.x = Math.round(focus.x / 100) * 100;
        this.floor.position.z = Math.round(focus.z / 100) * 100;
        this.apron.position.x = Math.round(focus.x / 60) * 60;
        this.apron.position.z = Math.round(focus.z / 60) * 60;

        // Distant terrain follows at 90% of the travel, so it parallaxes instead of feeling glued to the camera.
        this.distant.position.set(focus.x * 0.9, -2, focus.z * 0.9);

        // Motes drift and wrap around the camera volume.
        const positions = this.motes.geometry.attributes.position;
        const drift = 2 + speed * 0.22;
        for (let i = 0; i < positions.count; i++)
        {
            let x = positions.getX(i) + Math.sin(this.time * 0.3 + i) * 0.02 + drift * delta * 0.06;
            let y = positions.getY(i) + Math.sin(this.time * 0.6 + i * 1.7) * 0.02 * delta * 10;
            let z = positions.getZ(i);
            if (x > 75) { x -= 150; }
            if (x < -75) { x += 150; }
            positions.setXYZ(i, x, y, z);
        }
        positions.needsUpdate = true;
        this.motes.position.set(camera.position.x, camera.position.y - 6, camera.position.z);

        this.birdsClock += delta;
        for (const bird of this.birds)
        {
            const angle = bird.phase + this.birdsClock * bird.speed;
            const x = Math.cos(angle) * bird.radius;
            const z = Math.sin(angle) * bird.radius;
            bird.mesh.position.set(this.birdsOrigin.x + x, bird.height + Math.sin(this.time * 0.5 + bird.phase) * 3, this.birdsOrigin.z + z);
            bird.mesh.rotation.y = -angle + Math.PI / 2;
            bird.mesh.rotation.z = Math.sin(this.time * bird.flap + bird.phase) * 0.35;
        }
        this.birdsOrigin.set(Math.round(focus.x / 200) * 200, 0, Math.round(focus.z / 200) * 200);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       UTILITIES
//------------------------------------------------------------------------------------------------------------------------

function textureRepeat(texture, repeat)
{
    texture.repeat.set(repeat, repeat);
    texture.wrapS = THREE.RepeatWrapping;
    texture.wrapT = THREE.RepeatWrapping;
}

function mulberry(seed)
{
    let state = seed >>> 0;
    return () => {
        state = (state * 1664525 + 1013904223) >>> 0;
        return state / 4294967296;
    };
}

export { DustSprite };

//------------------------------------------------------------------------------------------------------------------------
//                                                       EFFECT LAYER
//------------------------------------------------------------------------------------------------------------------------

// Screen-space grit: wheel dust, landing puffs, boost streaks.
export class DustVeil
{
    constructor(scene, textures)
    {
        this.scene = scene;
        this.pool = [];
        const sprite = textures.dust;

        for (let i = 0; i < 90; i++)
        {
            const material = new THREE.SpriteMaterial({
                map: sprite,
                color: 0xe9d3ae,
                transparent: true,
                opacity: 0,
                depthWrite: false,
                blending: THREE.NormalBlending,
                fog: true,
            });
            const mesh = new THREE.Sprite(material);
            mesh.visible = false;
            this.scene.add(mesh);
            this.pool.push({ mesh, life: 0, duration: 1, velocity: new THREE.Vector3() });
        }
        this.cursor = 0;
    }

    Spawn(position, velocity, size, duration, color = 0xe9d3ae)
    {
        const particle = this.pool[this.cursor];
        this.cursor = (this.cursor + 1) % this.pool.length;
        particle.mesh.position.copy(position);
        particle.mesh.visible = true;
        particle.mesh.scale.setScalar(size);
        particle.mesh.material.color.setHex(color);
        particle.mesh.material.opacity = 0.42;
        particle.life = duration;
        particle.duration = duration;
        particle.velocity.copy(velocity);
    }

    update(delta)
    {
        for (const particle of this.pool)
        {
            if (!particle.mesh.visible)
            {
                continue;
            }
            particle.life -= delta;
            if (particle.life <= 0)
            {
                particle.mesh.visible = false;
                continue;
            }
            const τ = particle.life / particle.duration;
            particle.mesh.material.opacity = 0.38 * τ;
            particle.mesh.scale.multiplyScalar(1 + delta * 1.1);
            particle.mesh.position.addScaledVector(particle.velocity, delta);
            particle.velocity.y += 1.2 * delta;
        }
    }
}
