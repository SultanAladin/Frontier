/*==========================================================================================================================================
                                                           CAMERANAVIGATION.JS
==========================================================================================================================================*/
// 🧩 Camera-relative planar translation, explicit pointer panning, and opt-in keyboard navigation.

export function TranslateCamera(Camera,Intent,Δτ,Speed)
{
    const Length=Math.hypot(Intent.Right,Intent.Forward,Intent.Up);
    if(!Length)return;
    const Distance=Speed*Δτ/Length;
    Camera.Target[0]+=(Intent.Right*Math.cos(Camera.Yaw)-Intent.Forward*Math.sin(Camera.Yaw))*Distance;
    Camera.Target[2]+=(-Intent.Right*Math.sin(Camera.Yaw)-Intent.Forward*Math.cos(Camera.Yaw))*Distance;
    Camera.Target[1]+=Intent.Up*Distance;
}

export function InitializeCameraNavigation(API)
{
    const Find=Selector=>document.querySelector(Selector),Keys=new Set();
    let Mode='orbit',PreviousTime=null;
    const MovementKeys=new Set(['KeyW','KeyA','KeyS','KeyD','KeyQ','KeyE','ShiftLeft','ShiftRight']);
    const IsTyping=Target=>Target?.closest?.('input:not([type="checkbox"]),select,textarea,[contenteditable="true"]');
    function Clear(){Keys.clear();PreviousTime=null;}
    function SelectMode(Next)
    {
        Mode=Next;Find('#OrbitCamera').classList.toggle('active',Next==='orbit');Find('#PanCamera').classList.toggle('active',Next==='pan');
        API.Canvas.style.cursor=Next==='pan'?'grab':'default';
    }
    window.addEventListener('keydown',Event=>
    {
        if(API.RetrieveWorkspace()!=='landscape'||!Find('#KeyboardCamera').checked||IsTyping(Event.target)||document.querySelector('dialog[open]')||Event.ctrlKey||Event.metaKey||Event.altKey)return;
        if(!MovementKeys.has(Event.code))return;
        Keys.add(Event.code);Event.preventDefault();Event.stopImmediatePropagation();
    },true);
    window.addEventListener('keyup',Event=>Keys.delete(Event.code),true);
    window.addEventListener('blur',Clear);
    document.addEventListener('visibilitychange',Clear);
    Find('#KeyboardCamera').onchange=Clear;
    Find('#OrbitCamera').onclick=()=>SelectMode('orbit');Find('#PanCamera').onclick=()=>SelectMode('pan');
    function Step(Time)
    {
        const Δτ=PreviousTime===null?0:Math.max(0,Math.min(.05,(Time-PreviousTime)/1000));PreviousTime=Time;
        if(API.RetrieveWorkspace()!=='landscape'||!Find('#KeyboardCamera').checked||IsTyping(document.activeElement)||document.querySelector('dialog[open]'))Keys.clear();
        const Intent={Right:Number(Keys.has('KeyD'))-Number(Keys.has('KeyA')),Forward:Number(Keys.has('KeyW'))-Number(Keys.has('KeyS')),Up:Number(Keys.has('KeyQ'))-Number(Keys.has('KeyE'))};
        const Speed=Math.max(1,Math.min(2000,Number(Find('#CameraSpeed').value)||150));
        TranslateCamera(API.Camera,Intent,Δτ,Speed*(Keys.has('ShiftLeft')||Keys.has('ShiftRight')?3:1));
        Find('#CameraPosition').textContent=API.Camera.Target.map((Coordinate,Axis)=>`${['X','Y','Z'][Axis]} ${Coordinate.toFixed(1)}`).join(' · ');
        API.Canvas.dataset.cameraTarget=JSON.stringify(API.Camera.Target);
        API.Canvas.dataset.cameraYaw=String(API.Camera.Yaw);
    }
    return {RetrieveMode:()=>Mode,Step,Clear};
}
