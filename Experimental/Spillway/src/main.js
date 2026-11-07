/*====================================================================================================================================
                                                          MAIN.JS
====================================================================================================================================*/
// 🧩 Boot, stream the course, drive the chase camera, route events to the HUD, audio and dust.

import * as THREE from '../vendor/three.module.min.js';
import { CoursePath } from './path.js';
import { Ride } from './ride.js';
import { CreateMaterials, BuildChunk } from './geometry.js';
import { World, DustVeil } from './world.js';
import { Rider } from './rider.js';
import { RideAudio } from './audio.js';
import { Hud } from './hud.js';
import * as Textures from './textures.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

const BEST_KEY = 'spillway.best.v1';
const BUILD_AHEAD = 760;     // [m] - Course meshed beyond the rider
const KEEP_BEHIND = 150;     // [m] - Course kept behind the rider
const CHUNKS_PER_FRAME = 3;  // [-] - Smoothing for streaming hitches

function Clamp(value, low, high)
{
    return value < low ? low : (value > high ? high : value);
}

function Damp(current, target, rate, delta)
{
    return current + (target - current) * (1 - Math.exp(-rate * delta));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       APPLICATION
//------------------------------------------------------------------------------------------------------------------------

class Spillway
{
    constructor()
    {
        this.canvas = document.getElementById('stage');

        this.renderer = new THREE.WebGLRenderer({
            canvas: this.canvas,
            antialias: true,
            powerPreference: 'high-performance',
        });
        this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
        this.renderer.setSize(window.innerWidth, window.innerHeight);
        this.renderer.shadowMap.enabled = true;
        this.renderer.shadowMap.type = THREE.PCFSoftShadowMap;
        this.renderer.toneMapping = THREE.ACESFilmicToneMapping;
        this.renderer.toneMappingExposure = 1.0;
        this.renderer.outputColorSpace = THREE.SRGBColorSpace;

        this.scene = new THREE.Scene();
        this.camera = new THREE.PerspectiveCamera(58, window.innerWidth / window.innerHeight, 0.4, 8200);
        this.camera.position.set(0, 6, -12);

        this.textures = {
            concrete : Textures.ConcreteTexture(),
            desert   : Textures.DesertTexture(),
            water    : Textures.WaterTexture(),
            dust     : Textures.DustSprite(),
            beam     : Textures.BeamTexture(),
            sky      : Textures.SkyTexture(),
        };

        this.materials = CreateMaterials(this.textures);
        this.world = new World(this.scene, this.textures);
        this.dust = new DustVeil(this.scene, this.textures);

        this.hud = new Hud();
        this.audio = new RideAudio();

        this.course = new CoursePath(20261107);
        this.ride = new Ride(this.course);
        this.ride.running = true;

        this.rider = new Rider();
        this.scene.add(this.rider.root);

        this.chunkMeshes = new Map();
        this.buildQueue = [];
        this.chunkCursor = 0;

        this.mode = 'menu';
        this.lastTimestamp = performance.now();
        this.accumulator = 0;
        this.steer = 0;
        this.touchSteer = 0;
        this.touchDrift = false;
        this.shake = 0;
        this.cameraInitialised = false;
        this.lookTarget = new THREE.Vector3();
        this.cameraRoll = 0;
        this.dustTimer = 0;
        this.best = Number(window.localStorage.getItem(BEST_KEY) || 0);
        this.bestSavedAt = 0;
        this.topSpeed = 0;

        this.keys = new Set();
        this.bindInput();
        this.bindResize();

        this.hud.ShowTitle(this.best);
        this.PrimeChunks();
        this.EnsureChunks();
        this.SyncRider(0);
        this.PlaceMenuCamera(0);

        requestAnimationFrame((time) => this.Frame(time));
        this.hud.HideLoading();
    }

    //-------------------------------------------------------- Input -----------------------------------------------------------------

    bindInput()
    {
        window.addEventListener('keydown', (event) => {
            if (['ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'Space'].includes(event.code))
            {
                event.preventDefault();
            }

            this.keys.add(event.code);

            if (event.code === 'Enter' || event.code === 'Space')
            {
                if (this.mode === 'menu')
                {
                    this.StartRun();
                    return;
                }
            }
            if (event.code === 'Escape')
            {
                this.TogglePause();
            }
            if (event.code === 'KeyR' && this.mode === 'paused')
            {
                this.StartRun();
            }
            if (event.code === 'KeyM')
            {
                this.audio.setMuted(!this.audio.muted);
            }
        });

        window.addEventListener('keyup', (event) => this.keys.delete(event.code));
        window.addEventListener('blur', () => {
            this.keys.clear();
            this.touchDrift = false;
            this.touchSteer = 0;
        });

        document.getElementById('startButton').addEventListener('click', () => this.StartRun());
        document.getElementById('resumeButton').addEventListener('click', () => this.TogglePause());
        document.getElementById('restartButton').addEventListener('click', () => this.StartRun());

        // Touch controls.
        const coarse = window.matchMedia('(pointer: coarse)').matches;
        this.hud.SetTouchVisible(coarse);

        for (const button of document.querySelectorAll('.touch-pad__steer'))
        {
            const steerValue = Number(button.dataset.steer);
            const engage = (event) => {
                event.preventDefault();
                this.touchSteer = steerValue;
            };
            const release = (event) => {
                event.preventDefault();
                this.touchSteer = 0;
            };
            button.addEventListener('pointerdown', engage);
            button.addEventListener('pointerup', release);
            button.addEventListener('pointercancel', release);
            button.addEventListener('pointerleave', release);
        }

        const driftButton = document.getElementById('touchDrift');
        const engageDrift = (event) => {
            event.preventDefault();
            this.touchDrift = true;
        };
        const releaseDrift = (event) => {
            event.preventDefault();
            this.touchDrift = false;
        };
        driftButton.addEventListener('pointerdown', engageDrift);
        driftButton.addEventListener('pointerup', releaseDrift);
        driftButton.addEventListener('pointercancel', releaseDrift);
        driftButton.addEventListener('pointerleave', releaseDrift);
    }

    bindResize()
    {
        window.addEventListener('resize', () => {
            this.camera.aspect = window.innerWidth / window.innerHeight;
            this.camera.updateProjectionMatrix();
            this.renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
            this.renderer.setSize(window.innerWidth, window.innerHeight);
        });
    }

    ReadInput()
    {
        const left = this.keys.has('KeyA') || this.keys.has('ArrowLeft');
        const right = this.keys.has('KeyD') || this.keys.has('ArrowRight');
        const steerKey = (right ? 1 : 0) - (left ? 1 : 0);
        const driftKey = this.keys.has('Space') || this.keys.has('ShiftLeft') || this.keys.has('ShiftRight');
        return {
            steer : Clamp(steerKey + this.touchSteer, -1, 1),
            drift : driftKey || this.touchDrift,
        };
    }

    Autopilot()
    {
        const call = this.ride.cornerCallout();
        const curvature = this.course.Sample(this.ride.s).curv;
        const inCorner = Math.abs(curvature) > 0.004;
        return {
            steer : Clamp(-this.ride.u * 0.14 - this.ride.latVel * 0.11, -1, 1),
            drift : inCorner || (call.active && call.distance < this.ride.speed * 1.15),
        };
    }

    //-------------------------------------------------------- Run control -----------------------------------------------------------

    StartRun()
    {
        this.audio.Start();
        this.ride.reset();
        this.ride.speed = 21;
        this.ride.running = true;
        this.mode = 'playing';
        this.accumulator = 0;
        this.shake = 0;
        this.steer = 0;
        this.touchSteer = 0;
        this.touchDrift = false;
        this.topSpeed = 0;
        this.hud.HideTitle();
        this.hud.paused(false);
        this.hud.Flash('boost');
        this.SyncRider(0);
        this.PlaceChaseCamera(0, true);
    }

    TogglePause()
    {
        if (this.mode === 'playing')
        {
            this.mode = 'paused';
            this.hud.ShowPause({
                score     : this.ride.score,
                distance  : this.ride.distance,
                topSpeed  : this.ride.topSpeed,
                driftBest : this.ride.driftBest,
            });
            this.SaveBest(true);
        }
        else if (this.mode === 'paused')
        {
            this.mode = 'playing';
            this.hud.paused(false);
            this.accumulator = 0;
        }
    }

    SaveBest(force)
    {
        if (this.mode !== 'playing' && !force)
        {
            return;
        }
        if (this.ride.score > this.best + 1)
        {
            this.best = this.ride.score;
            this.hud.SetBest(this.best);
            try
            {
                window.localStorage.setItem(BEST_KEY, String(this.best));
            }
            catch
            {
                // Storage may be unavailable in private windows; score simply is not retained.
            }
        }
    }

    //-------------------------------------------------------- Course streaming ------------------------------------------------------

    PrimeChunks()
    {
        this.course.ExtendTo(1400);
    }

    EnsureChunks()
    {
        const distance = this.ride.s;
        this.course.ExtendTo(distance + BUILD_AHEAD + 220);

        for (let i = 0; i < this.course.chunks.length; i++)
        {
            const chunk = this.course.chunks[i];
            if (chunk.s0 > distance + BUILD_AHEAD)
            {
                break;
            }
            if (chunk.s1 < distance - KEEP_BEHIND)
            {
                continue;
            }
            if (!this.chunkMeshes.has(chunk.index))
            {
                this.buildQueue.push(chunk);
            }
        }
        let built = 0;
        while (this.buildQueue.length > 0 && built < CHUNKS_PER_FRAME)
        {
            const chunk = this.buildQueue.shift();
            if (this.chunkMeshes.has(chunk.index))
            {
                continue;
            }
            const group = BuildChunk(this.course, chunk, this.materials);
            this.scene.add(group);
            this.chunkMeshes.set(chunk.index, group);
            built += 1;
        }

        for (const [index, group] of this.chunkMeshes)
        {
            const chunk = this.course.chunks[index];
            if (!chunk || chunk.s1 < distance - KEEP_BEHIND - 120)
            {
                this.scene.remove(group);
                group.traverse((child) => {
                    if (child.geometry)
                    {
                        child.geometry.dispose();
                    }
                });
                this.chunkMeshes.delete(index);
            }
        }
    }

    ResetChunks()
    {
        this.buildQueue.length = 0;
        for (const group of this.chunkMeshes.values())
        {
            this.scene.remove(group);
            group.traverse((child) => {
                if (child.geometry)
                {
                    child.geometry.dispose();
                }
            });
        }
        this.chunkMeshes.clear();
    }

    //-------------------------------------------------------- Rider placement -------------------------------------------------------

    SyncRider(delta)
    {
        const sample = this.course.Sample(this.ride.s);
        const cos = Math.cos(sample.heading);
        const sin = Math.sin(sample.heading);
        const x = sample.x + cos * this.ride.u;
        const z = sample.z - sin * this.ride.u;
        const y = this.ride.y;

        this.riderPosition = new THREE.Vector3(x, y, z);
        this.riderForward = new THREE.Vector3(sin, 0, cos);
        this.riderRight = new THREE.Vector3(cos, 0, -sin);

        this.rider.setPosition(x, y, z, sample.heading + this.ride.slip);
        const speedFactor = Clamp(this.ride.speed / 52, 0, 1);
        this.rider.update(delta, this.ride.snapshot(), speedFactor);

        this.heading = sample.heading;
    }

    //-------------------------------------------------------- Cameras ---------------------------------------------------------------

    PlaceChaseCamera(delta, snap = false)
    {
        const speedFactor = Clamp(this.ride.speed / 52, 0, 1);
        const air = this.ride.grounded ? 0 : 1;
        const back = 6.4 + speedFactor * 2.1 + air * 1.6;
        const bend = Math.abs(this.course.Sample(this.ride.s + 12).curv);
        const height = 2.75 + speedFactor * 0.55 + air * 2.0 + Math.min(1.1, bend * 22);
        const forward = this.riderForward ?? new THREE.Vector3(0, 0, 1);
        const right = this.riderRight ?? new THREE.Vector3(1, 0, 0);

        const desired = new THREE.Vector3(
            this.riderPosition.x - forward.x * back + right.x * this.ride.u * 0.12,
            this.riderPosition.y + height,
            this.riderPosition.z - forward.z * back + right.z * this.ride.u * 0.12
        );

        const target = new THREE.Vector3(
            this.riderPosition.x + forward.x * 12,
            this.riderPosition.y + 1.5,
            this.riderPosition.z + forward.z * 12
        );

        if (snap || !this.cameraInitialised)
        {
            this.camera.position.copy(desired);
            this.lookTarget.copy(target);
            this.cameraInitialised = true;
        }
        else
        {
            this.camera.position.lerp(desired, 1 - Math.exp(-7.0 * delta));
            this.lookTarget.lerp(target, 1 - Math.exp(-7.5 * delta));
        }

        // Shake decays fast; the roll leans with the carve.
        const shake = this.shake;
        this.camera.position.x += (Math.random() - 0.5) * shake * 0.9;
        this.camera.position.y += (Math.random() - 0.5) * shake * 0.7;

        this.camera.lookAt(this.lookTarget);
        this.cameraRoll = Damp(this.cameraRoll, -this.ride.slip * 0.35 + (Math.random() - 0.5) * shake * 0.02, 6, delta);
        this.camera.rotateZ(this.cameraRoll);

        const desiredFov = 58 + speedFactor * 12 + (this.ride.boostTime > 0 ? 7 : 0) + air * 2;
        this.camera.fov = Damp(this.camera.fov, desiredFov, 4, delta);
        this.camera.updateProjectionMatrix();
    }

    PlaceMenuCamera(delta)
    {
        const time = performance.now() * 0.001;
        const sample = this.course.Sample(this.ride.s);
        const cos = Math.cos(sample.heading);
        const sin = Math.sin(sample.heading);
        const orbit = 0.9 + Math.sin(time * 0.12) * 0.5;
        const radius = 9.5;

        const desired = new THREE.Vector3(
            this.riderPosition.x + Math.sin(orbit) * radius - sin * 3.0,
            this.riderPosition.y + 3.4 + Math.sin(time * 0.35) * 0.4,
            this.riderPosition.z + Math.cos(orbit) * radius - cos * 3.0
        );
        this.camera.position.lerp(desired, 1 - Math.exp(-2.4 * delta));
        this.lookTarget.lerp(new THREE.Vector3(this.riderPosition.x, this.riderPosition.y + 1.25, this.riderPosition.z), 1 - Math.exp(-3.2 * delta));
        this.camera.lookAt(this.lookTarget);
        this.cameraRoll = Damp(this.cameraRoll, 0, 3, delta);
        this.camera.rotateZ(this.cameraRoll);
        this.camera.fov = Damp(this.camera.fov, 54, 3, delta);
        this.camera.updateProjectionMatrix();
    }

    //-------------------------------------------------------- Effects --------------------------------------------------------------

    RouteEvents(events)
    {
        for (const trigger of events)
        {
            this.audio.Event(trigger);

            switch (trigger.type)
            {
                case 'trick':
                    this.hud.Popup(trigger.label, trigger.points);
                    break;

                case 'land':
                    for (let i = 0; i < 6; i++)
                    {
                        const velocity = new THREE.Vector3((Math.random() - 0.5) * 3, Math.random() * 1.4, (Math.random() - 0.5) * 3);
                        this.dust.Spawn(this.riderPosition, velocity, 1.1 + Math.random() * 1.6, 0.6 + Math.random() * 0.4);
                    }
                    if (trigger.impact > 13)
                    {
                        this.shake = Math.max(this.shake, 0.28);
                    }
                    break;

                case 'slam':
                    this.shake = 0.85;
                    this.hud.Flash('wall');
                    for (let i = 0; i < 14; i++)
                    {
                        const velocity = new THREE.Vector3((Math.random() - 0.5) * 7, Math.random() * 3, (Math.random() - 0.5) * 7);
                        this.dust.Spawn(this.riderPosition, velocity, 2.2 + Math.random() * 2.4, 0.8 + Math.random() * 0.5);
                    }
                    break;

                case 'wall':
                    this.shake = 0.55;
                    this.hud.Flash('wall');
                    this.hud.Popup('Scuffed', 0);
                    break;

                case 'boost':
                    this.hud.Flash('boost');
                    this.shake = Math.max(this.shake, 0.22);
                    break;

                case 'splash':
                    this.hud.Flash('splash');
                    for (let i = 0; i < 16; i++)
                    {
                        const velocity = new THREE.Vector3((Math.random() - 0.5) * 8, 1 + Math.random() * 5, (Math.random() - 0.5) * 8);
                        this.dust.Spawn(this.riderPosition, velocity, 1.4 + Math.random() * 1.8, 0.7 + Math.random() * 0.5, 0x9fd8cf);
                    }
                    break;

                case 'milestone':
                    this.hud.Popup(`${trigger.value} m`, 0);
                    break;

                default:
                    break;
            }
        }
    }

    SprayDust(delta, state)
    {
        if (!state.grounded)
        {
            return;
        }
        const screeching = state.driftHeld || Math.abs(state.slip) > 0.09;
        if (!screeching)
        {
            return;
        }

        this.dustTimer -= delta;
        if (this.dustTimer > 0)
        {
            return;
        }
        this.dustTimer = 0.045;

        const origin = this.riderPosition.clone().addScaledVector(this.riderForward, -0.7);
        const velocity = new THREE.Vector3(
            -this.riderForward.x * 2 + (Math.random() - 0.5) * 2.5,
            0.7 + Math.random() * 1.4,
            -this.riderForward.z * 2 + (Math.random() - 0.5) * 2.5
        );
        this.dust.Spawn(origin, velocity, 0.8 + Math.random() * 0.9, 0.42 + Math.random() * 0.3, 0xdcc9a8);
    }

    //-------------------------------------------------------- Frame ----------------------------------------------------------------

    Frame(timestamp)
    {
        requestAnimationFrame((time) => this.Frame(time));

        const delta = Clamp((timestamp - this.lastTimestamp) / 1000, 0.0001, 0.06);
        this.lastTimestamp = timestamp;

        const input = this.mode === 'playing' ? this.ReadInput() : this.Autopilot();
        this.steer = Damp(this.steer, input.steer, 12, delta);

        if (this.mode !== 'paused')
        {
            this.accumulator += delta;
            const step = 1 / 120;
            let steps = 0;
            while (this.accumulator >= step && steps < 10)
            {
                if (this.mode === 'menu')
                {
                    const demo = this.Autopilot();
                    this.ride.update(step, { steer: demo.steer, drift: demo.drift });
                }
                else
                {
                    this.ride.update(step, { steer: this.steer, drift: input.drift });
                }
                this.accumulator -= step;
                steps += 1;

                if (this.ride.events.length > 0)
                {
                    this.RouteEvents(this.ride.events);
                }
            }

            if (steps === 10)
            {
                this.accumulator = 0;
            }

            this.SyncRider(delta);
            this.EnsureChunks();
            this.SprayDust(delta, this.ride.snapshot());

            this.topSpeed = Math.max(this.topSpeed, this.ride.speed);
            this.shake = Math.max(0, this.shake - delta * 2.6);
            this.bestSavedAt += delta;
            if (this.bestSavedAt > 3)
            {
                this.bestSavedAt = 0;
                this.SaveBest(false);
            }
        }

        const focus = this.riderPosition.clone().addScaledVector(this.riderForward, 20);
        this.world.update(delta, this.camera, focus, this.ride.speed);
        this.dust.update(delta);

        if (this.mode === 'menu')
        {
            this.PlaceMenuCamera(delta);
        }
        else
        {
            this.PlaceChaseCamera(delta, false);
        }

        if (this.mode !== 'menu')
        {
            const rush = Clamp((this.ride.speed - 20) / 32, 0, 1);
            this.hud.Update(this.ride.snapshot(), rush);
        }

        this.renderer.render(this.scene, this.camera);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

function ReportFailure(error)
{
    const sheet = document.getElementById('loadingSheet');
    if (!sheet)
    {
        return;
    }
    sheet.innerHTML = '';
    const box = document.createElement('pre');
    box.style.cssText = 'max-width:80vw;white-space:pre-wrap;font-family:monospace;font-size:12px;color:#3b2f22;text-align:left;line-height:1.6;';
    box.textContent = `SPILLWAY failed to start.\n\n${error?.stack ?? error}`;
    sheet.appendChild(box);
}

try
{
    const app = new Spillway();
    window.__spillway = app;
}
catch (error)
{
    ReportFailure(error);
    throw error;
}
