// Dependency-free loader for the retained standalone browser distribution.
const Caption = document.querySelector(".bring-up-caption");
Caption.textContent = "Checking application download · preview r4…";
const Application = new URL("../Archive/Application.js?preview=bundle-4", import.meta.url);
async function StartApplication()
{
    // Report the resource and status rather than the opaque dynamic-import chain error.
    const Response = await fetch(Application, { method: "HEAD", cache: "no-store", signal: AbortSignal.timeout(20000) });
    if (!Response.ok && Response.status !== 405)
        throw new Error(`${Application.pathname}: HTTP ${Response.status} ${Response.statusText}`);
    const Type = Response.headers.get("content-type") || "";
    if (Response.ok && !/javascript|ecmascript/i.test(Type))
        throw new Error(`${Application.pathname}: expected JavaScript, received ${Type || "no Content-Type"}`);
    Caption.textContent = "Loading application and physics · preview r4…";
    await import(Application.href);
}
StartApplication().catch(Failure =>
{
    console.error("Application module loading failed", Failure);
    document.getElementById("bring-up")?.classList.add("hidden");
    const Panel = document.getElementById("failure");
    if (!Panel) return;
    Panel.classList.remove("hidden");
    Panel.textContent = `Application modules could not load: ${Failure.message || Failure}. `
        + `Application URL: ${Application.href}. No node_modules or Runtime URL is required by this build. `
        + "If this resource is unavailable through the preview, reopen the port-8080 preview connection.";
});
