/*====================================================================================================================================
                                                    LAUNCH.MJS
====================================================================================================================================*/
// 🧩 Dependency-free static server for the water simulation prototype; ES modules and shaders need HTTP, not file://

import http from 'http';
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';

const Root = path.dirname(fileURLToPath(import.meta.url));
const Port = Number.parseInt(process.argv[2] ?? process.env.PORT ?? '8770', 10);
const Host = process.argv[3] ?? '0.0.0.0';

const ContentTypes =
{
    '.html': 'text/html; charset=utf-8',
    '.css':  'text/css; charset=utf-8',
    '.js':   'text/javascript; charset=utf-8',
    '.mjs':  'text/javascript; charset=utf-8',
    '.glsl': 'text/plain; charset=utf-8',
    '.vert': 'text/plain; charset=utf-8',
    '.frag': 'text/plain; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.png':  'image/png',
    '.svg':  'image/svg+xml'
};

const Server = http.createServer((Request, Response) =>
{
    const RequestPath = decodeURIComponent((Request.url ?? '/').split('?')[0]);
    const RelativePath = RequestPath === '/' ? 'index.html' : RequestPath.replace(/^\/+/, '');
    const FilePath = path.join(Root, RelativePath);

    if (!FilePath.startsWith(Root))
    {
        Response.writeHead(403).end('Forbidden');
        return;
    }

    fs.readFile(FilePath, (Failure, Payload) =>
    {
        if (Failure)
        {
            Response.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }).end(`Not found: ${RelativePath}`);
            return;
        }

        Response.writeHead(200, {
            'Content-Type':  ContentTypes[path.extname(FilePath).toLowerCase()] ?? 'application/octet-stream',
            'Cache-Control': 'no-store'
        });
        Response.end(Payload);
    });
});

Server.listen(Port, Host, () =>
{
    console.log(`Water simulation prototype: http://localhost:${Port}/`);
    console.log(`Bound to ${Host}:${Port} — open the page in a WebGL2 browser (Chrome, Edge, Firefox, Safari 15+).`);
});
