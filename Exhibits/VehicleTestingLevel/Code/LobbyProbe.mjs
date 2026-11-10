// 📝 Headless probe for the testing-level lobby. Run from Experimental/VehicleTestingLevel/Tools:
//    HOOKS=../../../Exhibits/VehicleTestingLevel/Code/LobbyProbe.mjs node HeadlessHarness.mjs .. 12 "?capture=1"
//    It reads the card exactly as a driver would see it, presses Enter, and reports the hand-over.
export default {
    BeforeFrame(Host, Frame)
    {
        if (Frame !== 3)
            return;
        console.log("card before Enter ·",
                    `visible=${Host.Lobby.Visible}`, `ready=${Host.Lobby.Ready}`,
                    `buttonDisabled=${document.getElementById("lobby-enter").disabled}`,
                    `status="${document.getElementById("lobby-status").textContent}"`,
                    `bar=${document.getElementById("lobby-progress-bar").style.width}`);
        Host.Lobby.Enter();
        console.log("after Enter ·", `visible=${Host.Lobby.Visible}`, `lobbyHidden=${document.getElementById("lobby").classList.contains("leaving")}`);
    },
    Report(Host)
    {
        console.log(`final · ready=${Host.Ready} frames=${Host.FrameIndex} lobbyVisible=${Host.Lobby.Visible}`);
    }
};
