/*====================================================================================================================================
                                                          RIDER.JS
====================================================================================================================================*/
// 🧩 Low-poly aqueduct rider: pigmented deck, clay-stained wheels, vermillion jacket, and a lean driven by slip angle and airtime.

import * as THREE from '../vendor/three.module.min.js';

const JACKET   = 0xc44a2e;
const JACKET_D = 0x8e3320;
const TROUSER  = 0xd8cdb4;
const SKIN     = 0xbd8763;
const HELMET   = 0x1f6b66;
const DECK     = 0x2f3336;
const DECK_ART = 0xd8a13a;
const SHOE     = 0x3a3f42;
const METAL    = 0x8d9296;

function box(width, height, depth, color, roughness = 0.85)
{
    const material = new THREE.MeshStandardMaterial({ color, roughness, metalness: 0.05 });
    const mesh = new THREE.Mesh(new THREE.BoxGeometry(width, height, depth), material);
    mesh.castShadow = true;
    mesh.receiveShadow = true;
    return mesh;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RIDER RIG
//------------------------------------------------------------------------------------------------------------------------

export class Rider
{
    constructor()
    {
        this.root = new THREE.Group();
        this.root.name = 'rider';

        this.boardPivot = new THREE.Group();
        this.boardPivot.position.y = 0.13;
        this.root.add(this.boardPivot);

        // Deck, kicked nose and tail, plywood edge visible from below.
        const deck = box(0.92, 0.075, 2.15, DECK, 0.75);
        this.boardPivot.add(deck);

        const art = box(0.86, 0.012, 1.4, DECK_ART, 0.6);
        art.position.y = -0.045;
        this.boardPivot.add(art);

        const nose = box(0.9, 0.07, 0.34, DECK, 0.75);
        nose.position.set(0, 0.045, 1.18);
        nose.rotation.x = -0.34;
        this.boardPivot.add(nose);

        const tail = box(0.9, 0.07, 0.32, DECK, 0.75);
        tail.position.set(0, 0.05, -1.15);
        tail.rotation.x = 0.4;
        this.boardPivot.add(tail);

        this.wheels = [];
        const wheelGeometry = new THREE.CylinderGeometry(0.13, 0.13, 0.16, 12);
        const wheelMaterial = new THREE.MeshStandardMaterial({ color: 0xd9c9a8, roughness: 0.95, metalness: 0 });
        const truckMaterial = new THREE.MeshStandardMaterial({ color: METAL, roughness: 0.45, metalness: 0.7 });

        for (const z of [-0.72, 0.72])
        {
            const truck = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.09, 0.16), truckMaterial);
            truck.position.set(0, -0.09, z);
            truck.castShadow = true;
            this.boardPivot.add(truck);

            for (const x of [-0.36, 0.36])
            {
                const wheel = new THREE.Mesh(wheelGeometry, wheelMaterial);
                wheel.rotation.z = Math.PI / 2;
                wheel.position.set(x, -0.14, z);
                wheel.castShadow = true;
                this.boardPivot.add(wheel);
                this.wheels.push(wheel);
            }
        }

        // Sideways stance: hips and shoulders rotate about the board axis.
        this.riderPivot = new THREE.Group();
        this.riderPivot.rotation.y = -0.92;
        this.riderPivot.position.y = 0.2;
        this.root.add(this.riderPivot);

        this.hips = new THREE.Group();
        this.hips.position.y = 0.52;
        this.riderPivot.add(this.hips);

        const pelvis = box(0.42, 0.2, 0.3, TROUSER, 0.9);
        this.hips.add(pelvis);

        this.torso = new THREE.Group();
        this.torso.position.y = 0.14;
        this.hips.add(this.torso);

        const chest = box(0.46, 0.56, 0.32, JACKET, 0.8);
        chest.position.y = 0.28;
        this.torso.add(chest);

        const panel = box(0.2, 0.42, 0.03, JACKET_D, 0.8);
        panel.position.set(0, 0.3, 0.17);
        this.torso.add(panel);

        const collar = box(0.34, 0.1, 0.28, TROUSER, 0.85);
        collar.position.y = 0.58;
        this.torso.add(collar);

        this.head = new THREE.Group();
        this.head.position.y = 0.72;
        this.torso.add(this.head);

        const skull = new THREE.Mesh(new THREE.SphereGeometry(0.145, 16, 12), new THREE.MeshStandardMaterial({ color: SKIN, roughness: 0.85 }));
        skull.castShadow = true;
        this.head.add(skull);

        const helmet = new THREE.Mesh(new THREE.SphereGeometry(0.175, 16, 12, 0, Math.PI * 2, 0, Math.PI * 0.62), new THREE.MeshStandardMaterial({ color: HELMET, roughness: 0.5, metalness: 0.1 }));
        helmet.position.y = 0.02;
        helmet.castShadow = true;
        this.head.add(helmet);

        const goggles = box(0.22, 0.06, 0.1, 0x2b3033, 0.3);
        goggles.position.set(0, 0.02, 0.13);
        this.head.add(goggles);

        // Arms held out for balance; the drift arm swings wide.
        this.arms = [];
        for (const side of [-1, 1])
        {
            const shoulder = new THREE.Group();
            shoulder.position.set(side * 0.27, 0.5, 0);
            this.torso.add(shoulder);

            const upper = box(0.11, 0.34, 0.12, JACKET, 0.8);
            upper.position.y = -0.16;
            shoulder.add(upper);

            const fore = box(0.09, 0.3, 0.1, SKIN, 0.85);
            fore.position.y = -0.44;
            shoulder.add(fore);

            this.arms.push({ group: shoulder, side });
        }

        // Legs braced in a wide surf stance.
        this.legs = [];
        for (const [side, z] of [[-1, 0.42], [1, -0.4]])
        {
            const hip = new THREE.Group();
            hip.position.set(side * 0.18, 0, z);
            this.hips.add(hip);

            const thigh = box(0.17, 0.42, 0.18, TROUSER, 0.9);
            thigh.position.y = -0.21;
            hip.add(thigh);

            const shin = box(0.15, 0.36, 0.16, TROUSER, 0.9);
            shin.position.y = -0.56;
            hip.add(shin);

            const shoe = box(0.17, 0.09, 0.36, SHOE, 0.8);
            shoe.position.set(0, -0.76, 0.06);
            hip.add(shoe);

            this.legs.push({ group: hip, side });
        }

        this.crouch = 0;
        this.roll = 0;
        this.time = 0;
    }

    //-------------------------------------------------------- Frame update ---------------------------------------------------------

    update(delta, state, speedFactor)
    {
        this.time += delta;

        const air = state.grounded ? 0 : 1;
        const drift = Math.abs(state.slip) > 0.04 ? 1 : 0;
        const targetCrouch = 0.12 + speedFactor * 0.18 + air * 0.18 + (state.driftHeld ? 0.14 : 0);
        this.crouch += (targetCrouch - this.crouch) * Math.min(1, delta * 6);
        this.roll += ((state.lean ?? 0) - this.roll) * Math.min(1, delta * 8);

        // Board pitches in flight, noses down on landing.
        const boardPitch = air ? Math.sin(this.time * 3.4) * 0.05 + 0.06 : 0;
        this.boardPivot.rotation.x = boardPitch + this.crouch * 0.08;
        this.boardPivot.rotation.z = this.roll * 0.55;

        this.hips.position.y = 0.52 - this.crouch * 0.22;
        this.torso.rotation.x = this.crouch * 0.45 + air * 0.12;
        this.torso.rotation.y = Math.sin(this.time * 1.4) * 0.04 + this.roll * 0.25;
        this.torso.rotation.z = -this.roll * 0.55;

        this.head.rotation.y = 0.32 - this.roll * 0.2;
        this.head.rotation.x = -0.1 - air * 0.12;

        for (const arm of this.arms)
        {
            const outward = arm.side > 0 ? 1 : -1;
            const balance = 0.5 + drift * 0.55 + air * 0.45;
            arm.group.rotation.z = outward * balance + this.roll * 0.5 * arm.side;
            arm.group.rotation.x = -0.35 + Math.sin(this.time * 2.2 + arm.side) * 0.06 + air * 0.2;
        }

        for (const leg of this.legs)
        {
            leg.group.rotation.x = this.crouch * 0.55;
            leg.group.rotation.z = leg.side * (0.06 + this.roll * 0.3);
        }

        for (const wheel of this.wheels)
        {
            wheel.rotation.x -= delta * (8 + speedFactor * 26);
        }
    }

    setPosition(x, y, z, heading)
    {
        this.root.position.set(x, y, z);
        this.root.rotation.y = heading;
    }
}
