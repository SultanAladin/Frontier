//============================================================================================================================================
//                                                              LEVELATLAS.JS
//============================================================================================================================================
// 🧩 Scene level catalogue for FrontierEngine — startup splash screen, in-editor level selector, and `?level=` / `?lobby=1` URL routing.

export const LevelAtlas = Object.freeze([
    {
        Key: "proving",
        Index: "01",
        Title: "Proving Grounds",
        FileStem: "ProvingGrounds",
        Subtitle: "216 × 120 m asphalt course · skidpad · jump wedge · smash alley · tunnel · drills",
        Summary: "Full vehicle dynamics and BVH ReSTIR GI testbed: main straight with raised kerbs and launch ramp, " +
                 "14 m skidpad, dark lit concrete tunnel, breakable smash alley, and east agility stations.",
        Features: Object.freeze(["Skidpad", "Jump wedge", "Smash alley", "Lit tunnel", "Slalom & moose", "BVH reflections"]),
        Relief: true,
        Lobby: false,
        Schematic: "proving"
    },
    {
        Key: "lobby",
        Index: "02",
        Title: "Lobby · Studio",
        FileStem: "LobbyStudio",
        Subtitle: "40 m automotive cyclorama cove · overhead softbox · 5 turntables · live player session",
        Summary: "Seamless photography cyclorama from the Vehicle HMI demo: suspended 24 × 11 m emissive softbox, " +
                 "hero turntable at centre, four live guest turntables with simulated players entering & leaving, and full Editor UI.",
        Features: Object.freeze(["Cyclorama cove", "24×11 m softbox", "5 turntables", "Live player joins/leaves", "3D rank plates", "Full Editor UI"]),
        Relief: false,
        Lobby: true,
        Schematic: "lobby"
    }
]);

export const DefaultLevelKey = "proving";

export function ResolveLevelKey(CandidateKey)
{
    const Normalized = String(CandidateKey || "").trim().toLowerCase();
    if (Normalized === "garage" || Normalized === "lobby" || Normalized === "studio")
        return "lobby";
    if (Normalized === "proving" || Normalized === "proving-grounds")
        return "proving";
    try
    {
        const Search = new URLSearchParams((globalThis.location && globalThis.location.search) || "");
        if (Search.get("lobby") === "1" || Search.get("level") === "lobby" || Search.get("level") === "garage")
            return "lobby";
    }
    catch {}
    return LevelAtlas.some((Entry) => Entry.Key === Normalized) ? Normalized : DefaultLevelKey;
}

export function QueryLevelEntry(CandidateKey)
{
    const Resolved = ResolveLevelKey(CandidateKey);
    return LevelAtlas.find((Entry) => Entry.Key === Resolved) || LevelAtlas[0];
}

export function FormatLevelSearch(TargetKey, AutoEnter = false)
{
    const Resolved = ResolveLevelKey(TargetKey);
    const Parameters = new URLSearchParams(typeof location !== "undefined" ? location.search : "");
    Parameters.set("level", Resolved);
    if (Resolved === "lobby")
        Parameters.set("lobby", "1");
    else
        Parameters.delete("lobby");
    if (AutoEnter)
        Parameters.set("autostart", "1");
    else
        Parameters.delete("autostart");
    return `?${Parameters.toString()}`;
}
