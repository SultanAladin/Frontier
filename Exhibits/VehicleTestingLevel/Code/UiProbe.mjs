// 📝 Headless probe for the level list, the lobby row, the way back and the independent editor columns. Run from
//    Experimental/VehicleTestingLevel/Tools:
//    HOOKS=../../../Exhibits/VehicleTestingLevel/Code/UiProbe.mjs node HeadlessHarness.mjs .. 12 "?capture=1&level=proving"
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
        const Status = () => document.getElementById("lobby-status").textContent;

        console.log("level list ·", `rows=${Rows.length}`,
                    `hubHere=${Hub.className.includes("here")}`,
                    `sceneCurrent=${Scene.className.includes("current")}`,
                    `sceneKey=${Scene.dataset.Level}`,
                    `sceneSelected=${Scene.classList.contains("selected")}`,
                    `hero="${document.getElementById("lobby-title").textContent}"`);

        Hub.click();
        console.log("click lobby row ·", `selected=${Hub.classList.contains("selected")}`,
                    `sceneSelected=${Scene.classList.contains("selected")}`,
                    `status="${Status()}"`, `lobbyVisible=${Host.Lobby.Visible}`);

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
        document.getElementById("lobby-return").click();
        console.log("click viewport Lobby button ·", `lobbyVisible=${Host.Lobby.Visible}`,
                    `sceneSelected=${document.getElementById("lobby-levels").children
                        .find((Row) => Row.dataset.Level !== "lobby").classList.contains("selected")}`);
        Host.Lobby.Enter();
    },
    Report(Host)
    {
        console.log(`final · ready=${Host.Ready} frames=${Host.FrameIndex} level=${Host.LevelKey} lobbyVisible=${Host.Lobby.Visible}`);
    }
};
