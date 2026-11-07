/*====================================================================================================================================
                                                           HUD.JS
====================================================================================================================================*/
// 🧩 Screen furniture: telemetry, drift charge, elbow callout, trick tickets, pause sheet. Pure DOM, no per-frame allocation.

function FormatInteger(value)
{
    return String(Math.round(value)).replace(/\B(?=(\d{3})+(?!\d))/g, ' ');
}

//------------------------------------------------------------------------------------------------------------------------
//                                                         HUD
//------------------------------------------------------------------------------------------------------------------------

export class Hud
{
    constructor()
    {
        this.titleScreen = document.getElementById('titleScreen');
        this.pauseSheet = document.getElementById('pauseSheet');
        this.loadingSheet = document.getElementById('loadingSheet');
        this.hudRoot = document.getElementById('hud');
        this.touchPad = document.getElementById('touchPad');

        this.score = document.getElementById('scoreValue');
        this.speed = document.getElementById('speedValue');
        this.distance = document.getElementById('distanceValue');
        this.multiplier = document.getElementById('multiplierValue');
        this.driftGauge = document.getElementById('driftGauge');
        this.driftFill = document.getElementById('driftFill');
        this.callout = document.getElementById('callout');
        this.calloutGlyph = document.getElementById('calloutGlyph');
        this.popups = document.getElementById('popups');
        this.flash = document.getElementById('flash');
        this.hintStrip = document.getElementById('hintStrip');
        this.bestValue = document.getElementById('bestValue');

        this.pauseScore = document.getElementById('pauseScore');
        this.pauseDistance = document.getElementById('pauseDistance');
        this.pauseSpeed = document.getElementById('pauseSpeed');
        this.pauseDrift = document.getElementById('pauseDrift');

        this.lastScore = -1;
        this.lastSpeed = -1;
        this.lastDistance = -1;
        this.lastMultiplier = -1;
        this.calloutLive = false;
        this.flashTimer = null;
        this.hintTimer = window.setTimeout(() => this.hintStrip?.classList.add('hint-strip--faded'), 11000);
    }

    //-------------------------------------------------------- Screens ---------------------------------------------------------------

    ShowTitle(best)
    {
        this.bestValue.textContent = FormatInteger(best);
        this.titleScreen.hidden = false;
        this.hudRoot.classList.remove('hud--visible');
        this.paused(false);
    }

    HideTitle()
    {
        this.titleScreen.hidden = true;
        this.hudRoot.classList.add('hud--visible');
        this.hintStrip?.classList.remove('hint-strip--faded');
        window.clearTimeout(this.hintTimer);
        this.hintTimer = window.setTimeout(() => this.hintStrip?.classList.add('hint-strip--faded'), 11000);
    }

    HideLoading()
    {
        this.loadingSheet.classList.add('sheet--gone');
        window.setTimeout(() => { this.loadingSheet.style.display = 'none'; }, 800);
    }

    paused(value)
    {
        this.pauseSheet.hidden = !value;
    }

    ShowPause(stats)
    {
        this.pauseScore.textContent = FormatInteger(stats.score);
        this.pauseDistance.textContent = `${FormatInteger(stats.distance)} m`;
        this.pauseSpeed.textContent = `${Math.round(stats.topSpeed * 3.6)} km/h`;
        this.pauseDrift.textContent = `${stats.driftBest.toFixed(1)} s`;
        this.paused(true);
    }

    SetTouchVisible(value)
    {
        this.touchPad.classList.toggle('touch-pad--live', value);
    }

    //-------------------------------------------------------- Frame update ----------------------------------------------------------

    Update(state, rush)
    {
        const speedKmh = Math.round(state.speed * 3.6);
        if (speedKmh !== this.lastSpeed)
        {
            this.speed.textContent = String(speedKmh);
            this.lastSpeed = speedKmh;
        }

        if (state.score !== this.lastScore)
        {
            this.score.textContent = FormatInteger(state.score);
            this.lastScore = state.score;
        }

        const metres = Math.floor(state.distance);
        if (metres !== this.lastDistance)
        {
            this.distance.innerHTML = `${FormatInteger(metres)}<span class="readout__unit">m</span>`;
            this.lastDistance = metres;
        }

        const multiplier = Math.round(state.multiplier * 10) / 10;
        if (multiplier !== this.lastMultiplier)
        {
            this.multiplier.textContent = `×${multiplier.toFixed(1)}`;
            this.multiplier.style.opacity = multiplier > 1.02 ? '1' : '0.45';
            this.lastMultiplier = multiplier;
        }

        const charge = Math.max(0, Math.min(1, state.driftCharge));
        this.driftFill.style.width = `${(charge * 100).toFixed(1)}%`;
        this.driftGauge.classList.toggle('drift-gauge--live', state.driftHeld);
        this.driftGauge.classList.toggle('drift-gauge--full', charge > 0.985);

        // Elbow callout only appears inside the braking window.
        const callout = state.callout;
        const lead = Math.max(45, state.speed * 2.0);
        const live = callout.active && callout.distance < lead;
        if (live !== this.calloutLive)
        {
            this.calloutLive = live;
            this.callout.classList.toggle('callout--live', live);
        }
        if (live)
        {
            this.calloutGlyph.textContent = callout.direction > 0 ? '▶' : '◀';
            const text = state.driftHeld ? 'Hold the carve' : 'Drift';
            if (this.callout.lastText !== text)
            {
                this.callout.lastText = text;
                this.callout.querySelector('.callout__text').textContent = text;
            }
        }

        this.hudRoot.style.setProperty('--rush', rush.toFixed(3));
    }

    //-------------------------------------------------------- Transients ------------------------------------------------------------

    Popup(label, points)
    {
        const node = document.createElement('div');
        node.className = 'popup';
        node.appendChild(document.createTextNode(label));
        if (points)
        {
            node.appendChild(document.createTextNode(' '));
            const bonus = document.createElement('b');
            bonus.textContent = `+${FormatInteger(points)}`;
            node.appendChild(bonus);
        }
        this.popups.appendChild(node);
        window.setTimeout(() => node.remove(), 1600);

        // Keep the stack short on long chains.
        while (this.popups.children.length > 4)
        {
            this.popups.firstElementChild.remove();
        }
    }

    Flash(kind)
    {
        this.flash.className = `hud__flash hud__flash--${kind}`;
        window.clearTimeout(this.flashTimer);
        this.flashTimer = window.setTimeout(() => { this.flash.className = 'hud__flash'; }, 240);
    }

    SetBest(best)
    {
        this.bestValue.textContent = FormatInteger(best);
    }
}
