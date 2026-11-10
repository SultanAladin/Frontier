// 📝 Headless probe for the level UI and the independent editor columns. Run from Experimental/VehicleTestingLevel/Tools:
//    HOOKS=../../../Exhibits/VehicleTestingLevel/Code/UiProbe.mjs node HeadlessHarness.mjs .. 12 "?capture=1&level=proving"
//    It reads the lobby's generated level list, closes one column at a time, folds both with P, and then enters the level.
const State = (Body) => `outlinerHidden=${Body.contains("outliner-hidden")} inspectorHidden=${Body.contains("inspector-hidden")}`;

export default {
    BeforeFrame(Host, Frame)
    {
        if (Frame !== 3)
            return;
        const Body = document.body.classList;
        const Rows = document.getElementById("lobby-levels").children;
        const Hub = Rows.find((Row) => Row.dataset.Level === "lobby");
        const Scene = Rows.find((Row) => Row.dataset.Level !== "lobby");
        console.log("level list ·", `rows=${Rows.length}`,
                    `hubHere=${!!Hub && Hub.className.includes("here")}`,
                    `sceneCurrent=${!!Scene && Scene.className.includes("current")}`,
                    `sceneKey=${Scene && Scene.dataset.Level}`,
                    `hero="${document.getElementById("lobby-title").textContent}"`);
        console.log("boot ·", State(Body));
        Host.Dock.Toggle("Outliner");
        console.log("close outliner ·", State(Body));
        Host.Dock.Toggle("Inspector");
        console.log("close inspector ·", State(Body));
        Host.Dock.Toggle("Outliner");
        console.log("reopen outliner ·", State(Body));
        Host.Dock.Toggle("Panels");
        console.log("P (restore both) ·", State(Body));
        Host.Dock.Toggle("Panels");
        console.log("P (fold both) ·", State(Body));
        Host.Dock.Toggle("Panels");
        Host.Lobby.Enter();
        console.log("entered level ·", `lobbyVisible=${Host.Lobby.Visible}`);
    },
    Report(Host)
    {
        console.log(`final · ready=${Host.Ready} frames=${Host.FrameIndex} level=${Host.LevelKey} lobbyVisible=${Host.Lobby.Visible}`);
    }
};
