/*====================================================================================================================================
                                                         PREVIEWHTTPSERVER.MJS
====================================================================================================================================*/
// 🧩 Static prototype serving with revalidating headers so embedded previews cannot mix cached revisions

import { createServer } from "node:http";
import { readFile, stat } from "node:fs/promises";
import path from "node:path";
import { fileURLToPath } from "node:url";

const PrototypeDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const Port = Number(process.env.PORT ?? 5173);
const MimeTypes = {
    ".html": "text/html; charset=utf-8",
    ".css": "text/css; charset=utf-8",
    ".js": "text/javascript; charset=utf-8",
    ".mjs": "text/javascript; charset=utf-8",
    ".json": "application/json; charset=utf-8",
    ".svg": "image/svg+xml",
    ".jpg": "image/jpeg",
    ".png": "image/png",
    ".woff2": "font/woff2",
    ".txt": "text/plain; charset=utf-8"
};

const Server = createServer(async (Request, Response) =>
{
    try
    {
        const RequestUrl = new URL(Request.url ?? "/", `http://${Request.headers.host ?? "localhost"}`);
        const DecodedPath = decodeURIComponent(RequestUrl.pathname);
        const RelativePath = DecodedPath === "/" ? "index.html" : DecodedPath.replace(/^\/+/, "");
        const AbsolutePath = path.resolve(PrototypeDirectory, RelativePath);
        if (!AbsolutePath.startsWith(PrototypeDirectory + path.sep) && AbsolutePath !== PrototypeDirectory)
        {
            Response.writeHead(403).end("Forbidden");
            return;
        }
        const FileStat = await stat(AbsolutePath);
        const FilePath = FileStat.isDirectory() ? path.join(AbsolutePath, "index.html") : AbsolutePath;
        const Body = await readFile(FilePath);
        Response.writeHead(200, {
            "Content-Type": MimeTypes[path.extname(FilePath)] ?? "application/octet-stream",
            "Content-Length": Body.length,
            "Cache-Control": "no-store, max-age=0",
            "X-Content-Type-Options": "nosniff"
        });
        Response.end(Request.method === "HEAD" ? undefined : Body);
    }
    catch
    {
        Response.writeHead(404, { "Cache-Control": "no-store" }).end("Not found");
    }
});

Server.listen(Port, "0.0.0.0", () =>
{
    console.log(`Frontier launcher preview: http://0.0.0.0:${Port} (no-store)`);
});
