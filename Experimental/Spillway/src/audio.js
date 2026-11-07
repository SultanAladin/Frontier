/*====================================================================================================================================
                                                          AUDIO.JS
====================================================================================================================================*/
// 🧩 Synthesised ride voice: wind, urethane roll, carve hiss, tunnel hush, landings, splash. No audio assets.

export class RideAudio
{
    constructor()
    {
        this.context = null;
        this.muted = false;
        this.ready = false;
    }

    // Must be called from a user gesture.
    Start()
    {
        if (this.ready)
        {
            this.context.resume();
            return;
        }

        const Context = window.AudioContext || window.webkitAudioContext;
        if (!Context)
        {
            return;
        }

        const context = new Context();
        this.context = context;

        this.master = context.createGain();
        this.master.gain.value = 0.55;
        this.master.connect(context.destination);

        this.noise = this.makeNoise(context, 2.5);

        // Wind: broadband pressure rising with speed.
        this.windFilter = context.createBiquadFilter();
        this.windFilter.type = 'bandpass';
        this.windFilter.frequency.value = 520;
        this.windFilter.Q.value = 0.7;
        this.windGain = context.createGain();
        this.windGain.gain.value = 0;
        this.source(context, this.noise, this.windFilter, this.windGain);

        // Roll: low urethane rumble.
        this.rollFilter = context.createBiquadFilter();
        this.rollFilter.type = 'lowpass';
        this.rollFilter.frequency.value = 260;
        this.rollFilter.Q.value = 2.2;
        this.rollGain = context.createGain();
        this.rollGain.gain.value = 0;
        this.source(context, this.noise, this.rollFilter, this.rollGain);

        // Carve hiss: bright scraping while drifting.
        this.carveFilter = context.createBiquadFilter();
        this.carveFilter.type = 'bandpass';
        this.carveFilter.frequency.value = 2600;
        this.carveFilter.Q.value = 1.4;
        this.carveGain = context.createGain();
        this.carveGain.gain.value = 0;
        this.source(context, this.noise, this.carveFilter, this.carveGain);

        // Tunnel hush: a narrow resonant layer that fades in under the roof.
        this.tunnelFilter = context.createBiquadFilter();
        this.tunnelFilter.type = 'bandpass';
        this.tunnelFilter.frequency.value = 180;
        this.tunnelFilter.Q.value = 6;
        this.tunnelGain = context.createGain();
        this.tunnelGain.gain.value = 0;
        this.source(context, this.noise, this.tunnelFilter, this.tunnelGain);

        this.ready = true;
    }

    makeNoise(context, seconds)
    {
        const length = Math.floor(context.sampleRate * seconds);
        const buffer = context.createBuffer(1, length, context.sampleRate);
        const data = buffer.getChannelData(0);
        let last = 0;
        for (let i = 0; i < length; i++)
        {
            const white = Math.random() * 2 - 1;
            last = (last + 0.02 * white) / 1.02;
            data[i] = last * 3.2;
        }
        return buffer;
    }

    source(context, buffer, filter, gain)
    {
        const node = context.createBufferSource();
        node.buffer = buffer;
        node.loop = true;
        node.connect(filter);
        filter.connect(gain);
        gain.connect(this.master);
        node.start();
        return node;
    }

    setMuted(muted)
    {
        this.muted = muted;
        if (this.ready)
        {
            this.master.gain.setTargetAtTime(muted ? 0 : 0.55, this.context.currentTime, 0.05);
        }
    }

    // Continuous layers follow the ride state every frame.
    update(state)
    {
        if (!this.ready)
        {
            return;
        }
        const τ = this.context.currentTime;
        const speedFactor = Math.min(1, state.speed / 52);
        const rush = speedFactor * speedFactor;

        this.windFilter.frequency.setTargetAtTime(360 + speedFactor * 900, τ, 0.12);
        this.windGain.gain.setTargetAtTime(0.05 + rush * 0.3 - (state.tunnel ? 0.12 : 0), τ, 0.15);

        this.rollGain.gain.setTargetAtTime(state.grounded ? 0.06 + speedFactor * 0.14 : 0.01, τ, 0.1);
        this.rollFilter.frequency.setTargetAtTime(180 + speedFactor * 260, τ, 0.1);

        const carving = state.driftHeld && state.grounded ? 1 : 0;
        this.carveGain.gain.setTargetAtTime(carving * (0.05 + Math.abs(state.slip) * 0.42), τ, 0.06);
        this.carveFilter.frequency.setTargetAtTime(2100 + Math.abs(state.slip) * 2600, τ, 0.08);

        this.tunnelGain.gain.setTargetAtTime(state.tunnel ? 0.22 : 0, τ, 0.25);
    }

    //-------------------------------------------------------- One-shots -------------------------------------------------------------

    burst(gainValue, filterType, frequency, duration, type = 'noise')
    {
        if (!this.ready || this.muted)
        {
            return;
        }
        const context = this.context;
        const τ = context.currentTime;

        if (type === 'noise')
        {
            const node = context.createBufferSource();
            node.buffer = this.noise;
            node.playbackRate.value = 1 + Math.random() * 0.3;

            const filter = context.createBiquadFilter();
            filter.type = filterType;
            filter.frequency.value = frequency;

            const gain = context.createGain();
            gain.gain.setValueAtTime(gainValue, τ);
            gain.gain.exponentialRampToValueAtTime(0.0001, τ + duration);

            node.connect(filter);
            filter.connect(gain);
            gain.connect(this.master);
            node.start(τ);
            node.stop(τ + duration + 0.05);
        }
    }

    tone(from, to, gainValue, duration, type = 'sine')
    {
        if (!this.ready || this.muted)
        {
            return;
        }
        const context = this.context;
        const τ = context.currentTime;
        const oscillator = context.createOscillator();
        oscillator.type = type;
        oscillator.frequency.setValueAtTime(from, τ);
        oscillator.frequency.exponentialRampToValueAtTime(Math.max(20, to), τ + duration);

        const gain = context.createGain();
        gain.gain.setValueAtTime(0.0001, τ);
        gain.gain.exponentialRampToValueAtTime(gainValue, τ + 0.015);
        gain.gain.exponentialRampToValueAtTime(0.0001, τ + duration);

        oscillator.connect(gain);
        gain.connect(this.master);
        oscillator.start(τ);
        oscillator.stop(τ + duration + 0.05);
    }

    Event(trigger)
    {
        switch (trigger.type)
        {
            case 'land':
                this.burst(0.32, 'lowpass', 900, 0.22);
                this.tone(120, 52, 0.16, 0.2);
                break;
            case 'slam':
                this.burst(0.55, 'lowpass', 620, 0.45);
                this.tone(90, 34, 0.3, 0.4, 'triangle');
                break;
            case 'wall':
                this.burst(0.4, 'bandpass', 1500, 0.24);
                this.tone(180, 70, 0.12, 0.18, 'square');
                break;
            case 'boost':
                this.burst(0.2, 'highpass', 1800, 0.55);
                this.tone(220, 780, 0.1, 0.45, 'triangle');
                break;
            case 'splash':
                this.burst(0.35, 'highpass', 2400, 0.5);
                break;
            case 'trick':
                this.tone(660, 990, 0.07, 0.16, 'triangle');
                break;
            default:
                break;
        }
    }
}
