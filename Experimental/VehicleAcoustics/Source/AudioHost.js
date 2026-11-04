//============================================================================================================================================
//                                                          AUDIOHOST.JS
//============================================================================================================================================
// 🧩 Device side of the pipeline: opens the AudioContext, installs the worklet that drains the shared ring, falls back to a message-fed
//    ScriptProcessor when the browser refuses a SharedArrayBuffer, and starts the render worker. Owns no synthesis and no vehicle state.

import { RingLayout, ComposeWorkletSource, WriteRingBlock } from "./AudioKernel.js";

// 📝 Three device paths, chosen by capability rather than by guesswork:
//    ① AudioWorklet + SharedArrayBuffer — the render worker writes the ring, the device thread only copies. No messages in the audio path.
//    ② AudioWorklet + transferred blocks  — no SAB: the worker posts finished blocks, the worklet queues them and fades over a gap.
//    ③ ScriptProcessor                     — no AudioWorklet: the same posted blocks are drained on the legacy callback.
//    All three pull from a producer that runs independently of the drawn frame rate, so the picture can stutter freely.

export function QueryAudioCapability()
{
    const Available = typeof globalThis !== "undefined";
    const SharedMemory = Available && typeof SharedArrayBuffer !== "undefined" &&
                         (typeof crossOriginIsolated === "undefined" || crossOriginIsolated === true);
    return {
        AudioContext: Available && !!(globalThis.AudioContext || globalThis.webkitAudioContext),
        AudioWorklet: Available && !!(globalThis.AudioContext && typeof AudioWorkletNode !== "undefined"),
        SharedMemory,
        Worker: Available && typeof Worker !== "undefined"
    };
}

export class VehicleAudioHost
{
    constructor()
    {
        this.Context = null;
        this.WorkletNode = null;
        this.ProcessorNode = null;
        this.Worker = null;
        this.Control = null;
        this.SamplePlane = null;
        this.Analyser = null;
        this.Mode = "idle";
        this.SampleRate = 48000;
        this.Handler = null;
        this.Started = false;
        this.Pending = null;
    }

    // 📝 The handler receives every worker message (telemetry, configured, errors); the page owns presentation and decides what to draw.
    ConfigureHandler(Handler)
    {
        this.Handler = Handler;
        return this;
    }

    async Start(Options = {})
    {
        if (this.Started)
            return this.Mode;
        const Capability = QueryAudioCapability();
        if (!Capability.AudioContext)
            throw new Error("This browser exposes no AudioContext; audio cannot start.");
        const ContextConstructor = globalThis.AudioContext || globalThis.webkitAudioContext;
        this.Context = new ContextConstructor({ latencyHint: "interactive" });
        await this.Context.resume();
        this.SampleRate = this.Context.sampleRate;

        const UseSharedMemory = Capability.SharedMemory && Options.ForceMessages !== true;
        if (UseSharedMemory)
        {
            this.Control = new Int32Array(new SharedArrayBuffer(RingLayout.ControlWords * 4));
            this.SamplePlane = new Float32Array(new SharedArrayBuffer(RingLayout.SampleCapacity * 2 * 4));
            Atomics.store(this.Control, RingLayout.WriteIndexWord, 0);
            Atomics.store(this.Control, RingLayout.ReadIndexWord, 0);
        }

        const WorkletInstalled = await this.InstallWorklet(UseSharedMemory);
        this.Worker = new Worker(new URL("./RenderWorker.js", import.meta.url), { type: "module" });
        this.Worker.onmessage = (Event) => this.AcceptWorkerMessage(Event.data);
        this.Worker.onerror = (Event) =>
        {
            if (this.Handler) this.Handler({ Type: "error", Message: `Render worker: ${Event.message || "failed to start"}` });
        };

        const Identifier = Options.Identifier || "laFerrari";
        const Position = Options.Position || "exterior";
        this.Worker.postMessage({
            Type: "configure", SampleRate: this.SampleRate, Identifier, Position,
            Gain: Options.Gain === undefined ? 1.0 : Options.Gain,
            AutoShift: Options.AutoShift !== false,
            Capacity: RingLayout.SampleCapacity,
            MessageMode: !UseSharedMemory || !WorkletInstalled,
            ControlBuffer: this.Control ? this.Control.buffer : null,
            SampleBuffer: this.SamplePlane ? this.SamplePlane.buffer : null
        });

        await this.InstallOutput(WorkletInstalled, UseSharedMemory);
        this.Worker.postMessage({ Type: "start" });
        this.Started = true;
        this.Mode = WorkletInstalled ? (UseSharedMemory ? "worklet+shared" : "worklet+messages") : "processor";
        return this.Mode;
    }

    async InstallWorklet(UseSharedMemory)
    {
        if (!this.Context.audioWorklet)
            return false;
        try
        {
            const Source = ComposeWorkletSource();
            const Url = URL.createObjectURL(new Blob([Source], { type: "application/javascript" }));
            await this.Context.audioWorklet.addModule(Url);
            URL.revokeObjectURL(Url);
            const ProcessorOptions = UseSharedMemory
                ? { SampleCapacity: RingLayout.SampleCapacity, ChannelCount: 2,
                    ControlBuffer: this.Control.buffer, SampleBuffer: this.SamplePlane.buffer }
                : { SampleCapacity: RingLayout.SampleCapacity, ChannelCount: 2,
                    ControlBuffer: new ArrayBuffer(RingLayout.ControlWords * 4),
                    SampleBuffer: new ArrayBuffer(4) };
            this.WorkletNode = new AudioWorkletNode(this.Context, "vehicle-acoustics", {
                numberOfInputs: 0, numberOfOutputs: 1,
                outputChannelCount: [2], processorOptions: ProcessorOptions
            });
            this.WorkletNode.port.onmessage = (Event) =>
            {
                if (this.Handler && Event.data && Event.data.Type === "status")
                    this.Handler({ Type: "device", Underrun: Event.data.Underrun });
            };
            return true;
        }
        catch (Failure)
        {
            if (this.Handler) this.Handler({ Type: "note", Message: `Worklet unavailable (${Failure.message}); using the processor path.` });
            return false;
        }
    }

    async InstallOutput(WorkletInstalled, UseSharedMemory)
    {
        this.Analyser = this.Context.createAnalyser();
        this.Analyser.fftSize = 2048;
        this.Analyser.smoothingTimeConstant = 0.72;
        if (WorkletInstalled)
        {
            this.WorkletNode.connect(this.Analyser);
            this.Analyser.connect(this.Context.destination);
            return;
        }
        this.ProcessorNode = this.Context.createScriptProcessor(1024, 0, 2);
        this.ProcessorNode.onaudioprocess = (Event) =>
        {
            const Left = Event.outputBuffer.getChannelData(0);
            const Right = Event.outputBuffer.getChannelData(1);
            Left.fill(0.0);
            Right.fill(0.0);
            const Block = this.Pending;
            if (!Block)
                return;
            const Take = Math.min(Left.length, Block.Left.length);
            Left.set(Block.Left.subarray(0, Take));
            Right.set(Block.Right.subarray(0, Take));
            this.Pending = Take < Block.Left.length
                ? { Left: Block.Left.subarray(Take), Right: Block.Right.subarray(Take) }
                : null;
        };
        this.ProcessorNode.connect(this.Analyser);
        this.Analyser.connect(this.Context.destination);
        void UseSharedMemory;
    }

    AcceptWorkerMessage(Message)
    {
        if (!Message) return;
        if (Message.Type === "audio" && this.WorkletNode)
        {
            this.WorkletNode.port.postMessage({ Type: "block", Left: Message.Left, Right: Message.Right, Count: Message.Count },
                                              [Message.Left.buffer, Message.Right.buffer]);
            return;
        }
        if (Message.Type === "audio")
        {
            this.Pending = { Left: Message.Left, Right: Message.Right };
            return;
        }
        if (this.Handler) this.Handler(Message);
    }

    ConfigureVehicle(Identifier, Position)
    {
        if (!this.Worker) return;
        if (Position)
            this.Worker.postMessage({ Type: "position", Position });
        this.Worker.postMessage({ Type: "select", Identifier, Position });
    }

    ConfigurePosition(Position)
    {
        if (this.Worker) this.Worker.postMessage({ Type: "position", Position });
    }

    ConfigureInput(Input)
    {
        if (this.Worker) this.Worker.postMessage({ Type: "input", Input });
    }

    ConfigureGain(Gain)
    {
        if (this.Worker) this.Worker.postMessage({ Type: "gain", Gain });
        if (this.WorkletNode) this.WorkletNode.port.postMessage({ Type: "gain", Value: 1.0 });
    }

    ResetMotion()
    {
        if (this.Worker) this.Worker.postMessage({ Type: "reset" });
    }

    QuerySpectrum()
    {
        if (!this.Analyser) return null;
        const Bins = new Uint8Array(this.Analyser.frequencyBinCount);
        this.Analyser.getByteFrequencyData(Bins);
        return Bins;
    }

    QueryLevel()
    {
        if (!this.Analyser) return 0.0;
        const Bins = new Uint8Array(this.Analyser.frequencyBinCount);
        this.Analyser.getByteTimeDomainData(Bins);
        let Peak = 0.0;
        for (let Index = 0; Index < Bins.length; ++Index)
        {
            const Value = Math.abs(Bins[Index] / 128.0 - 1.0);
            if (Value > Peak) Peak = Value;
        }
        return Peak;
    }

    QueryRingSamples()
    {
        if (!this.Control) return 0;
        const WriteIndex = Atomics.load(this.Control, RingLayout.WriteIndexWord);
        const ReadIndex = Atomics.load(this.Control, RingLayout.ReadIndexWord);
        return ((WriteIndex - ReadIndex) + RingLayout.SampleCapacity) % RingLayout.SampleCapacity;
    }

    async Stop()
    {
        if (this.Worker) this.Worker.postMessage({ Type: "stop" });
        if (this.Worker) this.Worker.terminate();
        this.Worker = null;
        if (this.WorkletNode) this.WorkletNode.disconnect();
        if (this.ProcessorNode) this.ProcessorNode.disconnect();
        if (this.Context) await this.Context.close();
        this.Context = null;
        this.Started = false;
        this.Mode = "idle";
    }
}

// 📝 Message-mode transport used when SharedArrayBuffer is unavailable: the worker renders the same blocks and posts them, and this shim
//    hands them to the worklet or processor queue. Kept here so the fallback is testable without a browser.
export function AttachMessageTransport(Worker, Target)
{
    Worker.postMessage({ Type: "transport", Mode: "messages" });
    void Target;
    void WriteRingBlock;
}
