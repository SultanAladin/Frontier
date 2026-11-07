/*====================================================================================================================================
                                                              RIDE.JS
====================================================================================================================================*/
// 🧩 Rider motion, drift charge, airtime, wall contact, scoring. Pure math so the harness can fly the course headlessly.

import { RIDE_GRAVITY, CoursePath } from './path.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const RIDE = {
    startSpeed   : 21.0,   // [m/s] - 76 km/h entry speed
    speedCeiling : 27.0,   // [m/s] - Added over distance, topping out near 173 km/h
    speedReach   : 900,    // [m] - Distance over which the ceiling is reached
    steerAccel   : 23.0,   // [m/s²] - Lateral steering authority
    grip         : 6.2,    // [1/s] - Lateral velocity damping when planted
    driftGrip    : 3.2,    // [1/s] - Reduced damping while carving
    driftAssist  : 0.35,   // [0-1] - How much of the corner's centrifugal load the drift cancels
    airAuthority : 0.38,   // [0-1] - Steering authority while airborne
    centrifugal  : 1.00,   // [0-1] - Arcade scaling of v²/R
    maxLateral   : 13.0,   // [m/s] - Lateral velocity ceiling
    wallMargin   : 0.75,   // [m] - Contact margin inside the lip
    boostPush    : 15.0,   // [m/s] - Extra speed above the target while boosting
    driftChargeRate : 1.35,// [1/s] - Charge gained per second in a corner
    comboGrace   : 2.4,    // [s] - Idle time before the multiplier decays
};

function Clamp(Value, Low, High)
{
    return Value < Low ? Low : (Value > High ? High : Value);
}

function Approach(Current, Target, Rate, δt)
{
    return Current + (Target - Current) * Clamp(Rate * δt, 0, 1);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       RIDE STATE
//------------------------------------------------------------------------------------------------------------------------

export class Ride
{
    constructor(course)
    {
        this.course = course ?? new CoursePath(20261107);
        this.events = [];
        this.reset();
    }

    reset()
    {
        this.course.ExtendTo(1400);
        this.s            = 0;      // [m]  - Arc distance along the course
        this.u            = 0;      // [m]  - Lateral offset from the centreline
        this.latVel       = 0;      // [m/s]
        this.y            = 0;      // [m]  - World height
        this.vy           = 0;      // [m/s]
        this.speed        = RIDE.startSpeed;
        this.targetSpeed  = RIDE.startSpeed;
        this.grounded     = true;
        this.airTime      = 0;      // [s]
        this.airPeak      = 0;      // [m]
        this.driftHeld    = false;
        this.driftTime    = 0;      // [s]
        this.driftCharge  = 0;      // [0-1]
        this.boostTime    = 0;      // [s]
        this.boostFlash   = 0;      // [0-1]
        this.wallFlash    = 0;      // [0-1]
        this.slip         = 0;      // [rad] - Visual yaw offset from the course heading
        this.lean         = 0;      // [rad] - Visual roll
        this.distance     = 0;      // [m]
        this.score        = 0;      // [pts]
        this.multiplier   = 1;      // [1-8]
        this.trickGrace   = 0;      // [s]
        this.boostCount   = 0;
        this.milestone    = 0;
        this.airCount     = 0;
        this.driftBest    = 0;      // [s] - Longest single carve
        this.topSpeed     = RIDE.startSpeed;
        this.bailCount    = 0;
        this.splashTime   = 0;
        this.tunnel       = false;
        this.running      = true;

        const sample = this.course.Sample(0);
        this.y = this.course.GroundHeight(sample, 0);
        this.vy = 0;
        this.events.length = 0;
    }

    //-------------------------------------------------------- Corner callout --------------------------------------------------------

    // Scans ahead for the next curvature onset so the HUD can warn before the 90° elbow arrives.
    cornerCallout(Reach = 130)
    {
        for (let d = 6; d <= Reach; d += 3)
        {
            const sample = this.course.Sample(this.s + d);
            if (Math.abs(sample.curv) > 0.008)
            {
                return { active: true, distance: d, direction: Math.sign(sample.curv), kind: sample.kind };
            }
        }
        return { active: false, distance: Reach, direction: 0, kind: this.course.Sample(this.s).kind };
    }

    //-------------------------------------------------------- Trick scoring ---------------------------------------------------------

    Trick(Points, Label)
    {
        const award = Math.round(Points * this.multiplier);
        this.score += award;
        this.trickGrace = RIDE.comboGrace;
        this.multiplier = Math.min(8, this.multiplier + 0.25);
        this.events.push({ type: 'trick', points: award, label: Label });
        return award;
    }

    //-------------------------------------------------------- Frame update ----------------------------------------------------------

    update(δt, input)
    {
        this.events.length = 0;
        if (!this.running)
        {
            return this.events;
        }

        const step = Math.min(δt, 1 / 30);
        this.course.ExtendTo(this.s + 1100);

        let sample = this.course.Sample(this.s);
        const steer = Clamp(input?.steer ?? 0, -1, 1);
        const wantsDrift = Boolean(input?.drift);

        // Speed: automatic throttle toward a distance-scaled ceiling, plus boost.
        this.targetSpeed = RIDE.startSpeed + RIDE.speedCeiling * Clamp(this.distance / RIDE.speedReach, 0, 1);
        this.boostTime = Math.max(0, this.boostTime - step);
        this.boostFlash = Math.max(0, this.boostFlash - step * 1.8);
        this.wallFlash = Math.max(0, this.wallFlash - step * 2.2);
        this.splashTime = Math.max(0, this.splashTime - step);

        const boosting = this.boostTime > 0;
        const desired = this.targetSpeed + (boosting ? RIDE.boostPush : 0);
        const throttle = this.speed < desired ? 2.6 : 1.5;
        this.speed = Approach(this.speed, desired, throttle, step);
        this.topSpeed = Math.max(this.topSpeed, this.speed);

        // Drift engagement. The commitment survives airtime so a rider can carry a carve through a roller.
        const canDrift = wantsDrift && this.speed > 12;
        if (canDrift && !this.driftHeld)
        {
            this.driftHeld = true;
            this.driftTime = 0;
            this.events.push({ type: 'driftStart' });
        }
        else if (!canDrift && this.driftHeld)
        {
            this.driftHeld = false;
            this.events.push({ type: 'driftEnd', value: this.driftTime });
            this.driftBest = Math.max(this.driftBest, this.driftTime);

            if (this.driftCharge > 0.45)
            {
                this.boostTime = Math.max(this.boostTime, 0.55 + this.driftCharge * 1.1);
                this.boostFlash = 1;
                this.boostCount += 1;
                this.Trick(900 * this.driftCharge, 'SLINGSHOT');
                this.events.push({ type: 'boost', value: this.driftCharge });
                this.driftCharge = 0;
            }
        }

        const drifting = this.driftHeld && this.grounded;
        if (drifting)
        {
            this.driftTime += step;
        }

        // Lateral dynamics: centrifugal load flings an un-carved rider outward.
        const curvature = sample.curv;
        const centrifugal = -curvature * this.speed * this.speed * RIDE.centrifugal;
        const assist = drifting ? (1 - RIDE.driftAssist) : 1;
        const damping = this.grounded ? (drifting ? RIDE.driftGrip : RIDE.grip) : 1.1;
        const authority = this.grounded ? 1 : RIDE.airAuthority;

        this.latVel += (centrifugal * assist + steer * RIDE.steerAccel * authority - this.latVel * damping) * step;
        this.latVel = Clamp(this.latVel, -RIDE.maxLateral, RIDE.maxLateral);
        this.u += this.latVel * step;

        // Lip contact.
        const limit = Math.max(2, sample.half - RIDE.wallMargin);
        if (Math.abs(this.u) > limit)
        {
            const contact = Math.abs(this.latVel);
            this.u = Math.sign(this.u) * limit;
            this.latVel *= -0.30;
            if (contact > 3.2)
            {
                this.speed *= 0.80;
                this.multiplier = 1;
                this.wallFlash = 1;
                this.driftCharge = Math.max(0, this.driftCharge - 0.5);
                this.events.push({ type: 'wall', value: contact });
            }
        }

        // Forward travel.
        this.s += this.speed * step;
        this.distance = this.s;

        // Vertical: a planted rider is carried by the slope until the surface falls away faster than gravity.
        const wasGrounded = this.grounded;
        sample = this.course.Sample(this.s);
        const ground = this.course.GroundHeight(sample, this.u);
        // 📝 Backward difference: sampling behind the rider keeps the launch velocity positive through a lip.
        // A forward difference sees the far side of the crest early and cancels the jump.
        const behindGround = this.course.GroundHeight(this.course.Sample(Math.max(0, this.s - 1.4)), this.u);
        const slopeRate = ((ground - behindGround) / 1.4) * this.speed;

        if (this.grounded)
        {
            // Ballistic prognosis using last frame's slope-following velocity.
            const ballistic = this.y + this.vy * step - 0.5 * RIDE_GRAVITY * step * step;
            if (ballistic > ground + 0.02)
            {
                this.grounded = false;
                this.vy -= RIDE_GRAVITY * step;
                this.y = ballistic;
                this.events.push({ type: 'launch', value: this.vy });
            }
            else
            {
                this.y = ground;
                this.vy = slopeRate;
            }
        }
        else
        {
            this.vy -= RIDE_GRAVITY * step;
            this.y += this.vy * step;
        }

        if (!this.grounded && this.y <= ground)
        {
            const impact = -this.vy;
            this.y = ground;
            this.grounded = true;

            if (!wasGrounded)
            {
                const duration = this.airTime;
                this.airTime = 0;
                const harsh = impact > 22 || Math.abs(this.latVel) > 12.6;

                if (duration > 0.32)
                {
                    this.airCount += 1;
                    if (harsh)
                    {
                        this.speed *= 0.86;
                        this.multiplier = 1;
                        this.wallFlash = 1;
                        this.bailCount += 1;
                        this.events.push({ type: 'slam', value: duration, impact });
                    }
                    else
                    {
                        this.Trick(340 * duration + 150 * this.airPeak, duration > 1.05 ? 'BIG AIR' : 'AIR');
                        this.events.push({ type: 'land', value: duration, impact });
                        if (this.driftHeld)
                        {
                            this.Trick(420, 'CARVED LANDING');
                        }
                        if (this.airPeak > 4.5)
                        {
                            this.boostTime = Math.max(this.boostTime, 0.45);
                            this.boostFlash = 1;
                        }
                    }
                }
                this.airPeak = 0;
            }

            this.vy = slopeRate;
            this.tunnel = sample.roof;
            if (sample.kind === 'basin' && this.splashTime <= 0 && !wasGrounded)
            {
                this.splashTime = 0.8;
                this.events.push({ type: 'splash' });
            }
        }
        if (!this.grounded)
        {
            this.airTime += step;
            this.airPeak = Math.max(this.airPeak, this.y - ground);
        }

        // Carve scoring and visual slip.
        if (drifting && Math.abs(curvature) > 0.004)
        {
            this.driftCharge = Math.min(1, this.driftCharge + RIDE.driftChargeRate * step * (0.6 + Math.abs(curvature) * 14));
            this.score += Math.round(140 * this.multiplier * step);
            this.trickGrace = RIDE.comboGrace;
            this.lean = Approach(this.lean, -Math.sign(curvature) * 0.34, 8, step);
        }
        else
        {
            this.lean = Approach(this.lean, steer * -0.18, 5, step);
        }

        this.slip = Approach(this.slip, Clamp(this.latVel * 0.035 + this.lean * 0.55, -0.5, 0.5), 7, step);

        // Combo decay.
        this.trickGrace -= step;
        if (this.trickGrace <= 0 && this.multiplier > 1)
        {
            this.multiplier = Math.max(1, this.multiplier - 0.9 * step);
        }

        // Milestones keep the run legible.
        const milestone = Math.floor(this.distance / 500);
        if (milestone > 0 && this.milestone !== milestone)
        {
            this.milestone = milestone;
            this.events.push({ type: 'milestone', value: milestone * 500 });
        }

        // Airborne riders keep facing the travel direction; grounded riders bank into the carve.
        return this.events;
    }

    //-------------------------------------------------------- Head-up snapshot -----------------------------------------------------

    snapshot()
    {
        const sample = this.course.Sample(this.s);
        return {
            s          : this.s,
            u          : this.u,
            y          : this.y,
            speed      : this.speed,
            grounded   : this.grounded,
            airTime    : this.airTime,
            driftHeld  : this.driftHeld,
            driftTime  : this.driftTime,
            driftCharge: this.driftCharge,
            boostTime  : this.boostTime,
            score      : this.score,
            multiplier : this.multiplier,
            distance   : this.distance,
            slip       : this.slip,
            lean       : this.lean,
            wallFlash  : this.wallFlash,
            boostFlash : this.boostFlash,
            tunnel     : sample.roof,
            kind       : sample.kind,
            heading    : sample.heading,
            half       : sample.half,
            callout    : this.cornerCallout(),
        };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       TERRAIN SAMPLE
//------------------------------------------------------------------------------------------------------------------------

// Convenience for the harness: advance a virtual rider straight down the course without inputs.
export function TraceStraight(course, Distance, Step = 0.25)
{
    const trail = [];
    for (let s = 0; s <= Distance; s += Step * 20)
    {
        const sample = course.Sample(s);
        trail.push([sample.x, sample.z, sample.y, sample.heading, sample.kind]);
    }
    return trail;
}
