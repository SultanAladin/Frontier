//============================================================================================================================================
//                                                             LEVELCATALOG.JS
//============================================================================================================================================
// 🧩 The levels this build can load — the one place a level is registered for the lobby, the level list and `?level=`.

// 📝 This tree ships one drivable level, but the level UI is data-driven: the lobby's list is built from this catalogue
//    and the active level comes from `?level=`, so adding a level is an entry here plus its scene content, not a new
//    switch in three files. The lobby is itself listed as a level (`Kind: "hub"`) and marked "Here" while it is open;
//    it is not a scene, so it can never be selected as one. An unknown or missing key resolves to the default scene,
//    so a stale bookmark or a hand-edited URL can never leave the app without a level.

export const LevelCatalog = Object.freeze([
    {
        Key: "lobby",
        Index: "00",
        Title: "Lobby",
        Kind: "hub",
        Subtitle: "Level select · the hub every level returns to"
    },
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

// 📝 The hub is a place in the list, not a scene: only these entries can be loaded by `?level=` or built at bring-up.
export const SceneLevelCatalog = Object.freeze(LevelCatalog.filter((Entry) => Entry.Kind !== "hub"));

export const DefaultLevelKey = SceneLevelCatalog[0].Key;

export function ResolveLevelKey(Key)
{
    return SceneLevelCatalog.some((Entry) => Entry.Key === Key) ? Key : DefaultLevelKey;
}

export function QueryLevel(Key)
{
    const Resolved = ResolveLevelKey(Key);
    return SceneLevelCatalog.find((Entry) => Entry.Key === Resolved);
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
