/*==========================================================================================================================================
                                                         VOLUMEPRESENTATION.JS
==========================================================================================================================================*/
// 🧩 Transfer-once GPU-only presentation, bounded view messages and fail-closed graphics ownership. Never renders pixels on the CPU.

const PresentationToken = Symbol.for('FrontierLandscape.Presentation');
export const GraphicsImageTimeoutMilliseconds = 10000;
export const GraphicsReplyTimeoutMilliseconds = 4000;

export function CreateVolumePresentation(Canvas, Options = {})
{
    if (Canvas[PresentationToken]) return Canvas[PresentationToken];
    let RasterizationThread = null, ThreadEpoch = 0, Disposed = false, Stopped = false, Suspended = false;
    let ViewPending = false, SubmittedRevision = 0, QueryToken = 0, AwaitingImage = true, ViewSubmitted = false;
    let VisibleWait = 0, LastTick = performance.now(), LastReply = performance.now();
    let ResolveInitialization, RejectInitialization, DeadlineTimer = null;
    const Queries = new Map();
    const ImageTimeout = Options.GraphicsImageTimeoutMilliseconds ?? GraphicsImageTimeoutMilliseconds;
    const ReplyTimeout = Options.GraphicsReplyTimeoutMilliseconds ?? GraphicsReplyTimeoutMilliseconds;
    const Initialization = new Promise((Resolve, Reject) => { ResolveInitialization = Resolve; RejectInitialization = Reject; });
    Initialization.catch(() => {});
    Canvas.dataset.graphicsImageTimeoutMilliseconds = String(ImageTimeout);
    Canvas.dataset.renderStatus = 'compiling';
    Canvas.dataset.renderTechnique = 'xyz-surface';

    function Status(Category, Message)
    {
        Canvas.dataset.renderStatus = Category;
        Canvas.dataset.renderMessage = Message || '';
        Options.Status?.(Category, Message);
    }

    function ReclaimThread(Failure)
    {
        ++ThreadEpoch;
        // 📝 Termination prevents future submissions. It is not a promise to cancel work already submitted to a physical GPU.
        RasterizationThread?.terminate();
        RasterizationThread = null;
        ViewPending = false;
        for (const Query of Queries.values()) { clearTimeout(Query.Timeout); Query.Reject(Failure); }
        Queries.clear();
    }

    function Failed(Failure, Category = 'failed')
    {
        if (Disposed || Stopped) return;
        Stopped = true;
        clearInterval(DeadlineTimer);
        const Message = Failure.message || String(Failure);
        RejectInitialization(Error(Message));
        ReclaimThread(Error(Message));
        Canvas.dataset.previewStopped = 'true';
        Canvas.dataset.renderMode ||= 'unavailable';
        Status(Category, Message);
    }

    function Receive(Reply)
    {
        LastReply = performance.now();
        const Telemetry = Reply.Telemetry || {};
        const ReplyRevision = Number(Telemetry.volumeRevision || 0);
        const Stale = ReplyRevision < SubmittedRevision;
        const CurrentImage = SubmittedRevision > 0 && Number(Telemetry.completedRevision) === SubmittedRevision;
        for (const [Name, Content] of Object.entries(Telemetry))
        {
            // 📝 UI preferences and navigation cannot be overwritten by acknowledgements for an older view.
            if (['previewWidth', 'viewSchedule', 'cameraTarget', 'cameraYaw'].includes(Name)) continue;
            if (!CurrentImage && Telemetry.renderStatus === 'ready' && ['renderStatus', 'renderMessage'].includes(Name)) continue;
            if (Stale && ['renderStatus', 'renderMessage', 'volumeRevision', 'completedRevision', 'completedQuality'].includes(Name)) continue;
            Canvas.dataset[Name] = Content;
        }
        if (Number(Telemetry.completedRevision) === SubmittedRevision && Number(Telemetry.terrainRenders) > 0)
            AwaitingImage = false;
        if (Reply.Category === 'view-complete') ViewPending = false;
        if (Reply.Category === 'status')
        {
            if (['failed', 'lost'].includes(Reply.Status))
            {
                Failed(Error(Reply.Message || 'GPU preview stopped. No software renderer is started.'), Reply.Status);
                return;
            }
            if (Reply.Status === 'ready' && (Stale || Number(Telemetry.completedRevision) !== SubmittedRevision)) return;
            if (['geometry', 'ready', 'refining'].includes(Reply.Status)) ResolveInitialization();
            Status(Reply.Status, Reply.Message);
        }
        if (Reply.Category === 'progress' && (!Reply.Revision || Reply.Revision === SubmittedRevision)) Options.Progress?.(Reply.Fraction);
        if (Reply.Category === 'inspection')
        {
            const Query = Queries.get(Reply.Token);
            if (!Query) return;
            Queries.delete(Reply.Token);
            clearTimeout(Query.Timeout);
            if (Reply.Failure) Query.Reject(Error(Reply.Failure));
            else Query.Resolve(Reply.Result);
        }
    }

    function ResetDeadlineClock()
    {
        LastTick = performance.now();
        LastReply = LastTick;
    }

    function EnforceImageDeadline()
    {
        const Now = performance.now(), Elapsed = Math.max(0, Now - LastTick);
        LastTick = Now;
        if (Disposed || Stopped || Suspended || document.hidden || !ViewSubmitted) return;
        if (AwaitingImage && SubmittedRevision > 0)
        {
            VisibleWait += Elapsed;
            if (VisibleWait >= ImageTimeout)
            {
                Failed(Error('GPU preview did not present the current scene within its time budget. Rendering stopped; no software fallback. Mask and save remain available.'));
                return;
            }
        }
        if (ViewPending && Now - LastReply > ReplyTimeout)
            Failed(Error('The GPU thread stopped responding. Further submissions are stopped; no software fallback.'));
    }

    function Upload(Layers)
    {
        if (Disposed || Stopped) return;
        SubmittedRevision++;
        AwaitingImage = true;
        VisibleWait = 0;
        ResetDeadlineClock();
        Canvas.dataset.volumeRevision = String(SubmittedRevision);
        RasterizationThread.postMessage({ Category: 'upload', Layers, Revision: SubmittedRevision });
    }

    function Rasterize(Camera, Seconds, Wave, Ocean, OceanTime = Seconds, Interacting = false, Detail = null)
    {
        if (Disposed || Stopped || Suspended || ViewPending) return;
        ViewPending = true;
        ViewSubmitted = true;
        RasterizationThread.postMessage({
            Category: 'view', Camera, Seconds, Wave, Ocean, OceanTime, Interacting, Detail,
            PreviewWidth: Canvas.dataset.previewWidth || '0',
            Display: { Width: Canvas.clientWidth, Height: Canvas.clientHeight, PixelRatio: devicePixelRatio, Visible: !document.hidden }
        });
    }

    function Suspend()
    {
        Suspended = true;
        ResetDeadlineClock();
        if (!Disposed && !Stopped) RasterizationThread.postMessage({ Category: 'suspend' });
    }

    function Resume()
    {
        Suspended = false;
        ResetDeadlineClock();
        if (!Disposed && !Stopped) RasterizationThread.postMessage({ Category: 'resume' });
    }

    function Inspect(Category = 'status')
    {
        if (Disposed) return Promise.reject(Error('Landscape preview was disposed.'));
        if (Stopped)
        {
            if (Category === 'status') return Promise.resolve({ ...Canvas.dataset });
            return Promise.reject(Error('GPU preview is stopped; explicit restart is required.'));
        }
        const Token = ++QueryToken;
        return new Promise((Resolve, Reject) =>
        {
            const Timeout = setTimeout(() =>
            {
                Queries.delete(Token);
                Reject(Error('The rendering thread did not answer the preview inspection.'));
            }, 10000);
            Queries.set(Token, { Resolve, Reject, Timeout });
            RasterizationThread.postMessage({ Category: 'inspect', Query: Category, Token });
        });
    }

    function Dispose()
    {
        if (Disposed) return;
        Disposed = true;
        clearInterval(DeadlineTimer);
        document.removeEventListener?.('visibilitychange', ResetDeadlineClock);
        RejectInitialization(Error('Landscape preview was disposed.'));
        ReclaimThread(Error('Landscape preview was disposed.'));
        delete Canvas[PresentationToken];
    }

    DeadlineTimer = setInterval(EnforceImageDeadline, 100);
    DeadlineTimer.unref?.();
    document.addEventListener?.('visibilitychange', ResetDeadlineClock);
    try
    {
        if (typeof Worker === 'undefined') throw Error('Dedicated GPU threads are unavailable. Preview stopped; no CPU rendering.');
        if (typeof Canvas.transferControlToOffscreen !== 'function')
            throw Error('OffscreenCanvas is unavailable. Preview stopped; no UI-thread GPU or CPU renderer.');
        const Surface = Canvas.transferControlToOffscreen();
        const Epoch = ++ThreadEpoch;
        RasterizationThread = new Worker(new URL('./VolumeSubmission.js', import.meta.url), { type: 'module' });
        RasterizationThread.onmessage = Event =>
        {
            if (!Disposed && !Stopped && Epoch === ThreadEpoch) Receive(Event.data);
        };
        RasterizationThread.onerror = Event =>
        {
            Event.preventDefault();
            if (!Disposed && Epoch === ThreadEpoch) Failed(Error(Event.message || 'The GPU thread failed.'));
        };
        RasterizationThread.onmessageerror = () =>
        {
            if (!Disposed && Epoch === ThreadEpoch) Failed(Error('The GPU thread could not receive the scene.'));
        };
        Canvas.dataset.renderExecution = 'thread';
        RasterizationThread.postMessage({ Category: 'initialize', Surface }, [Surface]);
    }
    catch (Failure) { Failed(Failure); }
    const Presentation = { Upload, Rasterize, Suspend, Resume, Failed, Dispose, Inspect, get Ready() { return Initialization; } };
    Canvas[PresentationToken] = Presentation;
    return Presentation;
}

export function InspectVolumePresentation(Canvas, Category = 'status')
{
    const Presentation = Canvas[PresentationToken];
    if (!Presentation) return Promise.reject(Error('Open Landscape before inspecting its preview.'));
    return Promise.resolve(Presentation.Inspect(Category));
}
