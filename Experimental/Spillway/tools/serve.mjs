/*====================================================================================================================================
                                                          SERVE.MJS
====================================================================================================================================*/
// 🧩 Zero-dependency static launcher for the prototype. Binds 0.0.0.0 so the sandbox preview proxy can reach it.

import { createServer } from 'node:http';
import { createReadStream, statSync } from 'node:fs';
import { extname, join, normalize, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('..', import.meta.url)));
const port = Number(process.env.PORT ?? process.argv[2] ?? 8080);
const host = process.env.HOST ?? '0.0.0.0';

const TYPES = {
    '.html' : 'text/html; charset=utf-8',
    '.js'   : 'text/javascript; charset=utf-8',
    '.mjs'  : 'text/javascript; charset=utf-8',
    '.css'  : 'text/css; charset=utf-8',
    '.json' : 'application/json; charset=utf-8',
    '.svg'  : 'image/svg+xml',
    '.png'  : 'image/png',
    '.md'   : 'text/markdown; charset=utf-8',
    '.txt'  : 'text/plain; charset=utf-8',
};

createServer((request, response) => {
    const requested = decodeURIComponent((request.url ?? '/').split('?')[0]);
    const relative = requested === '/' ? 'index.html' : requested.replace(/^\/+/, '');
    const path = resolve(join(root, normalize(relative)));

    if (!path.startsWith(root))
    {
        response.writeHead(403).end('Forbidden');
        return;
    }

    let stats;
    try
    {
        stats = statSync(path);
    }
    catch (error)
    {
        response.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }).end('Not found');
        return;
    }

    if (stats.isDirectory())
    {
        response.writeHead(302, { Location: `${requested.replace(/\/$/, '')}/index.html` }).end();
        return;
    }

    response.writeHead(200, {
        'Content-Type' : TYPES[extname(path)] ?? 'application/octet-stream',
        'Content-Length' : stats.size,
        'Cache-Control' : 'no-store',
    });
    createReadStream(path).pipe(response);
}).listen(port, host, () => {
    console.log(`Spillway served from ${root}`);
    console.log(`Listening on http://${host}:${port}`);
});
