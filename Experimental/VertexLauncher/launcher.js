/*====================================================================================================================================
                                                             LAUNCHER.JS
====================================================================================================================================*/
// 🧩 Storefront, library and local transfer-queue interaction for the Frontier launcher prototype

import { Catalogue, ResolveCatalogueEntry, FormatPrice } from "./Catalogue.mjs?v=20261102-3";
import {
    ConstructTransfer, AdvanceTransferQueue, PauseTransfer, ResumeTransfer, CancelTransfer,
    QueryTransferFraction, FormatMegabytes, FormatRemaining
} from "./TransferQueue.mjs?v=20261102-3";
import {
    AppendSpeedSample, EncodeSpeedTrace, ResolveEffectiveSpeed, ResolveRemainingSeconds,
    FormatDuration, ResolveStorageProjection
} from "./TransferMetrics.mjs?v=20261102-3";

const QueryElement = Identifier => document.getElementById(Identifier);
const QueryElements = Selector => [...document.querySelectorAll(Selector)];
const StorageKey = "frontier.launcher.v1";

const Persistent = ResolvePersistent();
const OwnedIdentifiers = new Set(Persistent.OwnedIdentifiers ?? Catalogue.filter(Entry => Entry.Owned).map(Entry => Entry.Identifier));
const InstalledIdentifiers = new Set(Persistent.InstalledIdentifiers ?? Catalogue.filter(Entry => Entry.Installed).map(Entry => Entry.Identifier));
const WishlistIdentifiers = new Set(Persistent.WishlistIdentifiers ?? []);
const UpdatedIdentifiers = new Set(Persistent.UpdatedIdentifiers ?? []);
let TransferQueue = Array.isArray(Persistent.TransferQueue) ? Persistent.TransferQueue : [
    ConstructTransfer(ResolveCatalogueEntry("alan-wake"), "install", 0.64),
    ConstructTransfer(ResolveCatalogueEntry("death-stranding"), "update", 0)
];
let CurrentView = "store";
let StoreTab = "discover";
let CategoryFilter = "all";
let LibraryFilter = "all";
let LibraryListEnabled = false;
let StoreCompactEnabled = false;
let FeaturedIndex = 0;
let DialogIdentifier = null;
let ToastTimer = null;
let AutoUpdateEnabled = Persistent.AutoUpdateEnabled ?? true;
let SpeedLimit = Persistent.SpeedLimit ?? 0;
let SpeedTrace = [ResolveEffectiveSpeed(TransferQueue.find(Transfer => Transfer.Condition === "active"), SpeedLimit)];

const FeaturedEntries = Catalogue.filter(Entry => Entry.Featured);

function ResolvePersistent()
{
    try
    {
        return JSON.parse(localStorage.getItem(StorageKey) ?? "{}");
    }
    catch
    {
        return {};
    }
}

function FinalizePersistent()
{
    try
    {
        localStorage.setItem(StorageKey, JSON.stringify({
            OwnedIdentifiers: [...OwnedIdentifiers],
            InstalledIdentifiers: [...InstalledIdentifiers],
            WishlistIdentifiers: [...WishlistIdentifiers],
            UpdatedIdentifiers: [...UpdatedIdentifiers],
            TransferQueue,
            AutoUpdateEnabled,
            SpeedLimit
        }));
    }
    catch { /* Private browsing keeps the prototype usable without persistence. */ }
}

function Icon(Identifier)
{
    return `<svg class="icon" aria-hidden="true"><use href="#${Identifier}"/></svg>`;
}

function EscapeText(Text)
{
    return String(Text).replace(/[&<>"']/g, Character => ({
        "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;"
    })[Character]);
}

function QueryActionLabel(GameEntry)
{
    if (!OwnedIdentifiers.has(GameEntry.Identifier)) return `Get ${FormatPrice(GameEntry.Price)}`;
    if (GameEntry.UpdateAvailable && !UpdatedIdentifiers.has(GameEntry.Identifier)) return "Update";
    if (InstalledIdentifiers.has(GameEntry.Identifier)) return "Play";
    if (TransferQueue.some(Transfer => Transfer.Identifier === GameEntry.Identifier && Transfer.Condition !== "completed")) return "Downloading";
    return "Install";
}

function ActivateGame(GameEntry)
{
    if (!OwnedIdentifiers.has(GameEntry.Identifier))
    {
        OwnedIdentifiers.add(GameEntry.Identifier);
        EnqueueTransfer(GameEntry, "install");
        Notify(`${GameEntry.Title} added to library — download started`);
    }
    else if (GameEntry.UpdateAvailable && !UpdatedIdentifiers.has(GameEntry.Identifier))
    {
        EnqueueTransfer(GameEntry, "update");
        Notify(`Updating ${GameEntry.Title}`);
    }
    else if (InstalledIdentifiers.has(GameEntry.Identifier))
    {
        Notify(`Launching ${GameEntry.Title} — prototype`);
    }
    else if (!TransferQueue.some(Transfer => Transfer.Identifier === GameEntry.Identifier && Transfer.Condition !== "completed"))
    {
        EnqueueTransfer(GameEntry, "install");
        Notify(`Downloading ${GameEntry.Title}`);
    }
    RefreshAll();
}

function EnqueueTransfer(GameEntry, Category)
{
    if (TransferQueue.some(Transfer => Transfer.Identifier === GameEntry.Identifier && Transfer.Condition !== "completed")) return;
    const Transfer = ConstructTransfer(GameEntry, Category, 0);
    Transfer.Condition = TransferQueue.some(Entry => Entry.Condition === "active") ? "queued" : "active";
    TransferQueue = [...TransferQueue.filter(Entry => Entry.Identifier !== GameEntry.Identifier), Transfer];
    FinalizePersistent();
}

function ToggleWishlist(GameEntry)
{
    if (WishlistIdentifiers.has(GameEntry.Identifier))
    {
        WishlistIdentifiers.delete(GameEntry.Identifier);
        Notify(`${GameEntry.Title} removed from wishlist`);
    }
    else
    {
        WishlistIdentifiers.add(GameEntry.Identifier);
        Notify(`${GameEntry.Title} added to wishlist`);
    }
    FinalizePersistent();
    RefreshAll();
}

function RenderStore()
{
    const SearchTerm = QueryElement("Search").value.trim().toLowerCase();
    const VisibleEntries = Catalogue.filter(GameEntry =>
    {
        const CategoryMatch = CategoryFilter === "all" || GameEntry.Category === CategoryFilter;
        const SearchMatch = !SearchTerm || `${GameEntry.Title} ${GameEntry.Studio} ${GameEntry.Genre}`.toLowerCase().includes(SearchTerm);
        const TabMatch = StoreTab !== "wishlist" || WishlistIdentifiers.has(GameEntry.Identifier);
        const DiscoverMatch = StoreTab !== "discover" || Boolean(SearchTerm) || GameEntry.Identifier !== "alan-wake";
        return CategoryMatch && SearchMatch && TabMatch && DiscoverMatch;
    });
    QueryElement("StoreGrid").innerHTML = VisibleEntries.map(GameEntry => RenderGameCard(GameEntry, "store")).join("");
    QueryElement("StoreGrid").classList.toggle("compact", StoreCompactEnabled);
    QueryElement("StoreEmpty").hidden = VisibleEntries.length > 0;
    QueryElement("Hero").hidden = StoreTab !== "discover" || Boolean(SearchTerm);
    QueryElement("CatalogueTitle").textContent = StoreTab === "wishlist" ? "Your wishlist" : StoreTab === "browse" ? "Browse games" : "Discover your next game";
}

function RenderGameCard(GameEntry, Context)
{
    const ActionLabel = QueryActionLabel(GameEntry);
    const Badge = Context === "library" && GameEntry.UpdateAvailable && !UpdatedIdentifiers.has(GameEntry.Identifier)
        ? '<span class="card-badge update">Update available</span>'
        : Context === "library" && InstalledIdentifiers.has(GameEntry.Identifier)
            ? '<span class="card-badge">Installed</span>'
            : "";
    const Meta = Context === "library"
        ? `<span class="meta-chip">${Icon("i-tag")}<span>${GameEntry.Genre}</span></span><span class="meta-chip">${Icon("i-drive")}<span>${GameEntry.Size}</span></span>`
        : `<span class="meta-chip">${Icon("i-tag")}<span>${GameEntry.Genre}</span></span><span class="game-price${GameEntry.Price === 0 ? " free" : ""}">${OwnedIdentifiers.has(GameEntry.Identifier) ? "In library" : FormatPrice(GameEntry.Price)}</span>`;
    const CardIcon = ActionLabel === "Play" ? "i-play" : ActionLabel === "Update" || ActionLabel === "Downloading" ? "i-download" : "i-plus";
    const WishlistEnabled = WishlistIdentifiers.has(GameEntry.Identifier);
    return `
        <article class="game-card" data-game="${GameEntry.Identifier}" data-context="${Context}" role="button" tabindex="0" aria-label="Open ${EscapeText(GameEntry.Title)}">
            <span class="game-cover">
                <img src="${GameEntry.Cover}" alt="${EscapeText(GameEntry.Title)} cover" loading="lazy">
                ${Badge}
            </span>
            <span class="game-card-copy">
                <span class="game-card-title">${EscapeText(GameEntry.Title)}</span>
                <span class="game-card-meta">${Meta}</span>
            </span>
            <span class="card-icon-row">
                <button class="card-icon primary" data-card-action="${GameEntry.Identifier}" aria-label="${EscapeText(ActionLabel)} ${EscapeText(GameEntry.Title)}">${Icon(CardIcon)}</button>
                <button class="card-icon" data-wishlist="${GameEntry.Identifier}" aria-label="${WishlistEnabled ? "Remove" : "Add"} ${EscapeText(GameEntry.Title)} ${WishlistEnabled ? "from" : "to"} wishlist" aria-pressed="${WishlistEnabled}">${Icon("i-heart")}</button>
                <button class="card-icon" data-open-game="${GameEntry.Identifier}" aria-label="Details for ${EscapeText(GameEntry.Title)}">${Icon("i-info")}</button>
            </span>
        </article>`;
}

function RenderLibrary()
{
    const SearchTerm = QueryElement("Search").value.trim().toLowerCase();
    const VisibleEntries = Catalogue.filter(GameEntry =>
    {
        if (!OwnedIdentifiers.has(GameEntry.Identifier)) return false;
        const SearchMatch = !SearchTerm || `${GameEntry.Title} ${GameEntry.Studio}`.toLowerCase().includes(SearchTerm);
        const FilterMatch = LibraryFilter === "all"
            || (LibraryFilter === "installed" && InstalledIdentifiers.has(GameEntry.Identifier))
            || (LibraryFilter === "updates" && GameEntry.UpdateAvailable && !UpdatedIdentifiers.has(GameEntry.Identifier));
        return SearchMatch && FilterMatch;
    });
    QueryElement("LibraryGrid").innerHTML = VisibleEntries.map(GameEntry => RenderGameCard(GameEntry, "library")).join("");
    QueryElement("LibraryGrid").classList.toggle("list", LibraryListEnabled);
    QueryElement("LibraryEmpty").hidden = VisibleEntries.length > 0;
    QueryElement("LibraryCount").textContent = OwnedIdentifiers.size;
}

function RenderHero()
{
    const GameEntry = FeaturedEntries[FeaturedIndex];
    QueryElement("HeroArt").src = GameEntry.Wide;
    QueryElement("HeroArt").alt = `${GameEntry.Title} key art`;
    QueryElement("HeroArt").style.objectPosition = GameEntry.Identifier === "control" ? "center 38%" : "center 30%";
    QueryElement("HeroTitle").textContent = GameEntry.Title;
    QueryElement("HeroDescription").textContent = GameEntry.Description;
    QueryElement("HeroAction").textContent = QueryActionLabel(GameEntry);
    QueryElement("HeroAction").dataset.game = GameEntry.Identifier;
    const WishlistEnabled = WishlistIdentifiers.has(GameEntry.Identifier);
    QueryElement("HeroWishlist").setAttribute("aria-pressed", String(WishlistEnabled));
    QueryElement("HeroWishlist").setAttribute("aria-label", `${WishlistEnabled ? "Remove" : "Add"} ${GameEntry.Title} ${WishlistEnabled ? "from" : "to"} wishlist`);
    QueryElement("HeroDots").innerHTML = FeaturedEntries.map((Entry, Index) =>
        `<button class="hero-dot${Index === FeaturedIndex ? " active" : ""}" data-featured="${Index}" role="tab" aria-selected="${Index === FeaturedIndex}" aria-label="Show ${EscapeText(Entry.Title)}"></button>`
    ).join("");
}

function RenderDownloads()
{
    const ActiveTransfers = TransferQueue.filter(Transfer => Transfer.Condition === "active" || Transfer.Condition === "paused");
    const QueuedTransfers = TransferQueue.filter(Transfer => Transfer.Condition === "queued");
    const CompletedTransfers = TransferQueue.filter(Transfer => Transfer.Condition === "completed");
    QueryElement("ActiveDownloads").innerHTML = ActiveTransfers.map(RenderActiveTransfer).join("");
    QueryElement("QueuedDownloads").innerHTML = QueuedTransfers.map(RenderQueuedTransfer).join("");
    QueryElement("CompletedDownloads").innerHTML = CompletedTransfers.map(RenderCompletedTransfer).join("");
    QueryElement("QueuedSection").hidden = QueuedTransfers.length === 0;
    QueryElement("CompletedSection").hidden = CompletedTransfers.length === 0;
    QueryElement("DownloadsEmpty").hidden = TransferQueue.length > 0;
    QueryElement("DownloadCount").textContent = ActiveTransfers.length + QueuedTransfers.length;
    const RunningCount = ActiveTransfers.filter(Transfer => Transfer.Condition === "active").length;
    QueryElement("DownloadsSummary").textContent = RunningCount > 0
        ? `${RunningCount} active download${RunningCount === 1 ? "" : "s"}`
        : ActiveTransfers.length > 0 ? "Downloads paused" : QueuedTransfers.length > 0 ? "Waiting in queue" : "All transfers complete";
    const PauseButton = QueryElement("PauseAll");
    PauseButton.hidden = ActiveTransfers.length === 0;
    PauseButton.textContent = RunningCount > 0 ? "Pause all" : "Resume all";
    RenderInsights();
    RenderSidebarTransfer();
}

function RenderInsights()
{
    const ActiveTransfer = TransferQueue.find(Transfer => Transfer.Condition === "active");
    const EffectiveSpeed = ResolveEffectiveSpeed(ActiveTransfer, SpeedLimit);
    const Trace = EncodeSpeedTrace(SpeedTrace);
    QueryElement("SpeedValue").textContent = EffectiveSpeed.toFixed(1);
    QueryElement("SpeedLine").setAttribute("points", Trace.Line);
    QueryElement("SpeedArea").setAttribute("points", Trace.Area);
    QueryElement("SpeedPeak").textContent = `Peak ${Trace.Peak.toFixed(0)} MB/s`;
    const RemainingSeconds = ResolveRemainingSeconds(TransferQueue, SpeedLimit);
    QueryElement("TimeLeftValue").textContent = FormatDuration(RemainingSeconds);
    const TotalPending = TransferQueue.filter(Transfer => Transfer.Condition !== "completed")
        .reduce((Total, Transfer) => Total + Transfer.TotalMegabytes, 0);
    const TransferredPending = TransferQueue.filter(Transfer => Transfer.Condition !== "completed")
        .reduce((Total, Transfer) => Total + Transfer.TransferredMegabytes, 0);
    QueryElement("TimeLeftMeter").style.width = TotalPending > 0 ? `${(TransferredPending / TotalPending) * 100}%` : "100%";
    QueryElement("TimeLeftNote").textContent = RemainingSeconds === null
        ? "Transfers are paused"
        : `${TransferQueue.filter(Transfer => Transfer.Condition !== "completed").length} transfers remaining`;

    const CompletedInstallMegabytes = TransferQueue
        .filter(Transfer => Transfer.Condition === "completed" && Transfer.Category !== "update")
        .reduce((Total, Transfer) => Total + Transfer.TotalMegabytes, 0);
    const InstalledMegabytes = Math.max(0, Catalogue
        .filter(GameEntry => InstalledIdentifiers.has(GameEntry.Identifier))
        .reduce((Total, GameEntry) => Total + GameEntry.Megabytes, 0) - CompletedInstallMegabytes);
    const Storage = ResolveStorageProjection(TransferQueue, InstalledMegabytes);
    QueryElement("StorageValue").textContent = (Storage.AvailableMegabytes / 1024).toFixed(1);
    QueryElement("StorageGamesValue").textContent = `${(Storage.GamesMegabytes / 1024).toFixed(1)} GB`;
    QueryElement("StorageSystemValue").textContent = `${(Storage.SystemMegabytes / 1024).toFixed(1)} GB`;
    QueryElement("StorageGamesBar").style.width = `${(Storage.GamesMegabytes / Storage.TotalMegabytes) * 100}%`;
    QueryElement("StorageSystemBar").style.width = `${(Storage.SystemMegabytes / Storage.TotalMegabytes) * 100}%`;
    const StorageCard = QueryElement("StorageCard");
    StorageCard.classList.toggle("warning", !Storage.Sufficient);
    StorageCard.classList.toggle("ok", Storage.Sufficient);
    QueryElement("StorageIcon").innerHTML = `<use href="#${Storage.Sufficient ? "i-check-circle" : "i-alert"}"/>`;
    QueryElement("StorageNote").textContent = Storage.Sufficient
        ? `Enough for ${FormatMegabytes(Storage.RequiredMegabytes)} queued`
        : `Need ${FormatMegabytes(Storage.RequiredMegabytes - Storage.AvailableMegabytes)} more`;
}

function RenderPreferences()
{
    QueryElement("PrefAutoUpdate").checked = AutoUpdateEnabled;
    QueryElements("#BandwidthLimit [data-bandwidth]").forEach(Button =>
        Button.setAttribute("aria-pressed", String(Number(Button.dataset.bandwidth) === SpeedLimit)));
    QueryElement("BandwidthNote").textContent = SpeedLimit > 0 ? `Limited to ${SpeedLimit} MB/s` : "Unlimited — full line speed";
}

function EnqueueAutomaticUpdates()
{
    if (!AutoUpdateEnabled) return;
    for (const GameEntry of Catalogue)
    {
        if (!OwnedIdentifiers.has(GameEntry.Identifier) || !InstalledIdentifiers.has(GameEntry.Identifier)) continue;
        if (!GameEntry.UpdateAvailable || UpdatedIdentifiers.has(GameEntry.Identifier)) continue;
        EnqueueTransfer(GameEntry, "update");
    }
}

function RenderActiveTransfer(Transfer)
{
    const Percent = Math.floor(QueryTransferFraction(Transfer) * 100);
    const Paused = Transfer.Condition === "paused";
    const EffectiveSpeed = ResolveEffectiveSpeed(Transfer, SpeedLimit);
    const SpeedText = Paused ? "Paused" : `${EffectiveSpeed.toFixed(1)} MB/s`;
    return `
        <article class="download-card${Paused ? " paused" : ""}" data-transfer="${Transfer.Identifier}">
            <img src="${Transfer.Cover}" alt="">
            <div class="download-copy">
                <div class="download-title-row"><h3>${EscapeText(Transfer.Title)}</h3><span class="download-percent">${Percent}%</span></div>
                <p class="download-status">${Transfer.Category === "update" ? "Updating" : "Downloading"} · ${FormatMegabytes(Transfer.TransferredMegabytes)} of ${FormatMegabytes(Transfer.TotalMegabytes)}</p>
                <div class="download-progress" role="progressbar" aria-label="${EscapeText(Transfer.Title)} download" aria-valuemin="0" aria-valuemax="100" aria-valuenow="${Percent}"><i style="width:${Percent}%"></i></div>
                <div class="download-metrics"><span><strong>${SpeedText}</strong></span><span>${FormatRemaining(Transfer, SpeedLimit)}</span></div>
            </div>
            <div class="download-controls">
                <button class="icon-button" data-transfer-action="${Paused ? "resume" : "pause"}" data-transfer-id="${Transfer.Identifier}" aria-label="${Paused ? "Resume" : "Pause"} ${EscapeText(Transfer.Title)}">${Icon(Paused ? "i-play" : "i-pause")}</button>
                <button class="icon-button" data-transfer-action="cancel" data-transfer-id="${Transfer.Identifier}" aria-label="Cancel ${EscapeText(Transfer.Title)}">${Icon("i-close")}</button>
            </div>
        </article>`;
}

function RenderQueuedTransfer(Transfer)
{
    return `
        <article class="download-card queued" data-transfer="${Transfer.Identifier}">
            <img src="${Transfer.Cover}" alt="">
            <div class="download-copy"><div class="download-title-row"><h3>${EscapeText(Transfer.Title)}</h3></div><p class="download-status">${Transfer.Category === "update" ? "Update" : "Install"} · ${FormatMegabytes(Transfer.TotalMegabytes)} · Queued</p></div>
            <div class="download-controls"><button class="icon-button" data-transfer-action="cancel" data-transfer-id="${Transfer.Identifier}" aria-label="Remove ${EscapeText(Transfer.Title)} from queue">${Icon("i-close")}</button></div>
        </article>`;
}

function RenderCompletedTransfer(Transfer)
{
    return `
        <article class="download-card completed" data-transfer="${Transfer.Identifier}">
            <img src="${Transfer.Cover}" alt="">
            <div class="download-copy"><div class="download-title-row"><h3>${EscapeText(Transfer.Title)}</h3><span class="download-percent">Complete</span></div><p class="download-status">${FormatMegabytes(Transfer.TotalMegabytes)} · Ready to play</p></div>
            <div class="download-controls"><button class="icon-button" data-transfer-action="play" data-transfer-id="${Transfer.Identifier}" aria-label="Play ${EscapeText(Transfer.Title)}">${Icon("i-play")}</button></div>
        </article>`;
}

function RenderSidebarTransfer()
{
    const Transfer = TransferQueue.find(Entry => Entry.Condition === "active")
        ?? TransferQueue.find(Entry => Entry.Condition === "paused")
        ?? TransferQueue.find(Entry => Entry.Condition === "queued");
    const SidebarTransfer = QueryElement("SidebarTransfer");
    SidebarTransfer.hidden = !Transfer;
    if (!Transfer) return;
    const Percent = Math.floor(QueryTransferFraction(Transfer) * 100);
    QueryElement("SidebarTransferArt").src = Transfer.Cover;
    QueryElement("SidebarTransferTitle").textContent = Transfer.Title;
    QueryElement("SidebarTransferDetail").textContent = Transfer.Condition === "paused" ? `Paused · ${Percent}%` : Transfer.Condition === "queued" ? "Queued" : `Downloading · ${Percent}%`;
    QueryElement("SidebarTransferBar").style.width = `${Percent}%`;
    QueryElement("SidebarTransferPercent").textContent = `${Percent}%`;
    QueryElement("SidebarTransfer").style.setProperty("--transfer-progress", String(Percent));
    QueryElement("SidebarTransfer").setAttribute("aria-label", `Open ${Transfer.Title} download, ${Percent}%`);
}

function RefreshAll()
{
    RenderStore();
    RenderLibrary();
    RenderHero();
    RenderDownloads();
    RenderPreferences();
    if (DialogIdentifier && QueryElement("GameDialog").open) RefreshDialog();
    FinalizePersistent();
}

function SwitchView(ViewName)
{
    CurrentView = ViewName;
    QueryElements(".view").forEach(View => View.classList.toggle("active", View.id === `View${ViewName[0].toUpperCase()}${ViewName.slice(1)}`));
    QueryElements(".nav-link[data-view]").forEach(Button =>
    {
        const Active = Button.dataset.view === ViewName;
        Button.classList.toggle("active", Active);
        if (Active) Button.setAttribute("aria-current", "page");
        else Button.removeAttribute("aria-current");
    });
    QueryElement("PageLabel").textContent = ViewName[0].toUpperCase() + ViewName.slice(1);
    QueryElement("Search").value = "";
    QueryElement("Search").placeholder = ViewName === "downloads" ? "Search games" : ViewName === "library" ? "Search library" : "Search games";
    QueryElement("Main").focus({ preventScroll: true });
    window.scrollTo({ top: 0, behavior: "instant" });
    RefreshAll();
}

function OpenGameDialog(Identifier)
{
    DialogIdentifier = Identifier;
    RefreshDialog();
    QueryElement("GameDialog").showModal();
}

function RefreshDialog()
{
    const GameEntry = ResolveCatalogueEntry(DialogIdentifier);
    if (!GameEntry) return;
    QueryElement("DialogArt").src = GameEntry.Cover;
    QueryElement("DialogArt").alt = `${GameEntry.Title} cover`;
    QueryElement("DialogGenre").textContent = `${GameEntry.Studio} · ${GameEntry.Genre}`;
    QueryElement("DialogTitle").textContent = GameEntry.Title;
    QueryElement("DialogDescription").textContent = GameEntry.Description;
    QueryElement("DialogMeta").innerHTML = `<span>${GameEntry.Size}</span><span>${GameEntry.Genre}</span><span>${OwnedIdentifiers.has(GameEntry.Identifier) ? "In library" : FormatPrice(GameEntry.Price)}</span>`;
    QueryElement("DialogAction").textContent = QueryActionLabel(GameEntry);
    QueryElement("DialogWishlist").textContent = WishlistIdentifiers.has(GameEntry.Identifier) ? "Remove from wishlist" : "Add to wishlist";
}

function Notify(Message)
{
    const Toast = QueryElement("Toast");
    Toast.textContent = Message;
    Toast.hidden = false;
    clearTimeout(ToastTimer);
    ToastTimer = setTimeout(() => { Toast.hidden = true; }, 2800);
}

function CompleteTransfers()
{
    let Changed = false;
    for (const Transfer of TransferQueue)
    {
        if (Transfer.Condition !== "completed") continue;
        const GameEntry = ResolveCatalogueEntry(Transfer.Identifier);
        if (!GameEntry) continue;
        if (Transfer.Category === "update")
        {
            UpdatedIdentifiers.add(Transfer.Identifier);
        }
        else
        {
            InstalledIdentifiers.add(Transfer.Identifier);
        }
        Changed = true;
    }
    if (Changed) FinalizePersistent();
}

/*------------------------------------------------------------------------------------------------------------------------
                                                        EVENT WIRING
------------------------------------------------------------------------------------------------------------------------*/

QueryElements("[data-view]").forEach(Button => Button.addEventListener("click", () => SwitchView(Button.dataset.view)));
QueryElement("SidebarTransfer").addEventListener("click", () => SwitchView("downloads"));
QueryElement("HeroPrevious").addEventListener("click", () => { FeaturedIndex = (FeaturedIndex - 1 + FeaturedEntries.length) % FeaturedEntries.length; RenderHero(); });
QueryElement("HeroNext").addEventListener("click", () => { FeaturedIndex = (FeaturedIndex + 1) % FeaturedEntries.length; RenderHero(); });
QueryElement("HeroDots").addEventListener("click", Event =>
{
    const Dot = Event.target.closest("[data-featured]");
    if (!Dot) return;
    FeaturedIndex = Number(Dot.dataset.featured);
    RenderHero();
});
QueryElement("HeroAction").addEventListener("click", () => ActivateGame(FeaturedEntries[FeaturedIndex]));
QueryElement("HeroWishlist").addEventListener("click", () => ToggleWishlist(FeaturedEntries[FeaturedIndex]));
QueryElements("[data-store-tab]").forEach(Button => Button.addEventListener("click", () =>
{
    StoreTab = Button.dataset.storeTab;
    QueryElements("[data-store-tab]").forEach(Tab =>
    {
        const Active = Tab === Button;
        Tab.classList.toggle("active", Active);
        Tab.setAttribute("aria-selected", String(Active));
    });
    RenderStore();
}));
QueryElement("CategoryPills").addEventListener("click", Event =>
{
    const Button = Event.target.closest("[data-category]");
    if (!Button) return;
    CategoryFilter = Button.dataset.category;
    QueryElements("[data-category]").forEach(Pill => Pill.classList.toggle("active", Pill === Button));
    RenderStore();
});
QueryElement("LibraryFilters").addEventListener("click", Event =>
{
    const Button = Event.target.closest("[data-library-filter]");
    if (!Button) return;
    LibraryFilter = Button.dataset.libraryFilter;
    QueryElements("[data-library-filter]").forEach(Pill => Pill.classList.toggle("active", Pill === Button));
    RenderLibrary();
});
QueryElement("LibraryViewToggle").addEventListener("click", () =>
{
    LibraryListEnabled = !LibraryListEnabled;
    QueryElement("LibraryViewToggle").setAttribute("aria-pressed", String(LibraryListEnabled));
    QueryElement("LibraryViewToggle").setAttribute("aria-label", LibraryListEnabled ? "Switch to grid view" : "Switch to list view");
    QueryElement("LibraryViewToggle").innerHTML = Icon(LibraryListEnabled ? "i-grid" : "i-list");
    RenderLibrary();
});
QueryElement("StoreDensityToggle").addEventListener("click", () =>
{
    StoreCompactEnabled = !StoreCompactEnabled;
    QueryElement("StoreDensityToggle").setAttribute("aria-pressed", String(StoreCompactEnabled));
    QueryElement("StoreDensityToggle").setAttribute("aria-label", StoreCompactEnabled ? "Switch to comfortable grid" : "Switch to compact grid");
    QueryElement("StoreDensityToggle").innerHTML = Icon(StoreCompactEnabled ? "i-grid" : "i-compact");
    RenderStore();
});
QueryElement("Search").addEventListener("input", () => { RenderStore(); RenderLibrary(); });
document.addEventListener("click", Event =>
{
    const CardAction = Event.target.closest("[data-card-action]");
    if (CardAction)
    {
        Event.preventDefault();
        Event.stopPropagation();
        ActivateGame(ResolveCatalogueEntry(CardAction.dataset.cardAction));
        return;
    }
    const WishlistAction = Event.target.closest("[data-wishlist]");
    if (WishlistAction)
    {
        Event.preventDefault();
        Event.stopPropagation();
        ToggleWishlist(ResolveCatalogueEntry(WishlistAction.dataset.wishlist));
        return;
    }
    const OpenAction = Event.target.closest("[data-open-game]");
    if (OpenAction)
    {
        Event.preventDefault();
        Event.stopPropagation();
        OpenGameDialog(OpenAction.dataset.openGame);
        return;
    }
    const Card = Event.target.closest("[data-game]");
    if (Card && !Event.target.closest("#HeroAction")) OpenGameDialog(Card.dataset.game);
});
document.addEventListener("keydown", Event =>
{
    const Card = Event.target.closest(".game-card");
    if (Card && (Event.key === "Enter" || Event.key === " ") && !Event.target.closest("button"))
    {
        Event.preventDefault();
        OpenGameDialog(Card.dataset.game);
    }
});
QueryElement("DialogClose").addEventListener("click", () => QueryElement("GameDialog").close());
QueryElement("DialogAction").addEventListener("click", () => ActivateGame(ResolveCatalogueEntry(DialogIdentifier)));
QueryElement("DialogWishlist").addEventListener("click", () => ToggleWishlist(ResolveCatalogueEntry(DialogIdentifier)));
QueryElement("GameDialog").addEventListener("click", Event =>
{
    if (Event.target === QueryElement("GameDialog")) QueryElement("GameDialog").close();
});
QueryElement("GameDialog").addEventListener("close", () => { DialogIdentifier = null; });
QueryElement("PauseAll").addEventListener("click", () =>
{
    const Running = TransferQueue.some(Transfer => Transfer.Condition === "active");
    if (Running)
    {
        TransferQueue = TransferQueue.map(Transfer => Transfer.Condition === "active" ? { ...Transfer, Condition: "paused" } : Transfer);
    }
    else
    {
        const FirstPaused = TransferQueue.find(Transfer => Transfer.Condition === "paused");
        TransferQueue = FirstPaused ? ResumeTransfer(TransferQueue, FirstPaused.Identifier) : TransferQueue;
    }
    RefreshAll();
});
QueryElement("ViewDownloads").addEventListener("click", Event =>
{
    const Button = Event.target.closest("[data-transfer-action]");
    if (!Button) return;
    const Identifier = Button.dataset.transferId;
    const Action = Button.dataset.transferAction;
    if (Action === "pause") TransferQueue = PauseTransfer(TransferQueue, Identifier);
    if (Action === "resume") TransferQueue = ResumeTransfer(TransferQueue, Identifier);
    if (Action === "cancel") TransferQueue = CancelTransfer(TransferQueue, Identifier);
    if (Action === "play")
    {
        const GameEntry = ResolveCatalogueEntry(Identifier);
        if (GameEntry) Notify(`Launching ${GameEntry.Title} — prototype`);
    }
    RefreshAll();
});
function OpenSettings()
{
    QueryElement("AutoUpdate").checked = AutoUpdateEnabled;
    QueryElement("SpeedLimit").value = String(SpeedLimit);
    QueryElement("SettingsDialog").showModal();
}
QueryElement("SettingsButton").addEventListener("click", OpenSettings);
QueryElement("TopSettings").addEventListener("click", OpenSettings);
QueryElement("SettingsClose").addEventListener("click", () => QueryElement("SettingsDialog").close());
QueryElement("AutoUpdate").addEventListener("change", Event =>
{
    AutoUpdateEnabled = Event.target.checked;
    if (AutoUpdateEnabled) EnqueueAutomaticUpdates();
    RefreshAll();
});
QueryElement("SpeedLimit").addEventListener("change", Event =>
{
    SpeedLimit = Number(Event.target.value);
    FinalizePersistent();
    RenderDownloads();
    RenderPreferences();
});
QueryElement("PrefAutoUpdate").addEventListener("change", Event =>
{
    AutoUpdateEnabled = Event.target.checked;
    if (AutoUpdateEnabled) EnqueueAutomaticUpdates();
    RefreshAll();
});
QueryElement("BandwidthLimit").addEventListener("click", Event =>
{
    const Button = Event.target.closest("[data-bandwidth]");
    if (!Button) return;
    SpeedLimit = Number(Button.dataset.bandwidth);
    FinalizePersistent();
    RenderDownloads();
    RenderPreferences();
});
document.addEventListener("keydown", Event =>
{
    if (Event.key === "/" && !Event.target.matches("input, select, textarea"))
    {
        Event.preventDefault();
        QueryElement("Search").focus();
    }
});

setInterval(() =>
{
    const PreviousQueue = TransferQueue;
    TransferQueue = AdvanceTransferQueue(TransferQueue, 0.5, SpeedLimit);
    const ActiveTransfer = TransferQueue.find(Transfer => Transfer.Condition === "active");
    SpeedTrace = AppendSpeedSample(SpeedTrace, ResolveEffectiveSpeed(ActiveTransfer, SpeedLimit));
    const Completed = TransferQueue.some((Transfer, Index) => Transfer.Condition === "completed" && PreviousQueue[Index]?.Condition !== "completed");
    if (Completed)
    {
        CompleteTransfers();
        Notify("Download complete — ready to play");
        RefreshAll();
    }
    else
    {
        RenderDownloads();
        if (CurrentView === "downloads") FinalizePersistent();
    }
}, 500);

EnqueueAutomaticUpdates();
RefreshAll();
