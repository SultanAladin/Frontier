//============================================================================================================================================
//                                                             LEVELCATALOG.JS
//============================================================================================================================================
// 🧩 The levels this build can load — the one place a level is registered for the lobby, the level button and `?level=`.

// 📝 This tree ships a single level, but the level UI is data-driven: the lobby's level list is built from this catalogue
//    and the active level comes from `?level=`, so adding a level is an entry here plus its scene content, not a new
//    switch in three files. An unknown or missing key resolves to the default, so a stale bookmark or a hand-edited URL
//    can never leave the app without a scene.

export const LevelCatalog = Object.freeze([
    {
        Key: "proving",
        Index: "01",
        Title: "Proving Grounds",
        Subtitle: "Skidpad · jump wedge · smash alley · tunnel · slalom · training drills",
        Lede: "One level, one car, no menus between them. The reduction carries the driving demo only: the Project Drive " +
              "ControlVehicle, its cabin and tablet HMI, and the asphalt proving grounds it was tuned on.",
        Contents: ["Skidpad", "Jump wedge", "Smash alley", "Tunnel", "Slalom", "Training drills"]
    }
]);

export const DefaultLevelKey = "proving";

// 📝 Unknown keys fall back to the shipped level rather than to a blank scene.
export function ResolveLevelKey(Key)
{
    return LevelCatalog.some((Entry) => Entry.Key === Key) ? Key : DefaultLevelKey;
}

export function QueryLevel(Key)
{
    const Resolved = ResolveLevelKey(Key);
    return LevelCatalog.find((Entry) => Entry.Key === Resolved);
}

// 📝 Switching levels rebuilds the scene at bring-up, so the switch is a URL change: the new level then boots behind the
//    lobby, which reports its own progress. `lobby=0` is dropped so the player sees that bring-up.
export function ConstructLevelSearch(Key)
{
    const Parameters = new URLSearchParams(location.search);
    Parameters.set("level", ResolveLevelKey(Key));
    Parameters.delete("lobby");
    return `?${Parameters.toString()}`;
}
