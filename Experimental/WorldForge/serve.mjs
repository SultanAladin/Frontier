/*============================================================================================================================================
                                                          SERVE.MJS
============================================================================================================================================*/
// 🧩 Dependency-free static host for the world editor; binds all interfaces for the live preview proxy

import { createServer } from 'node:http';
import { readFile, stat } from 'node:fs/promises';
import { extname, join, normalize, resolve, sep } from 'node:path';

const RootDirectory = resolve(new URL('.', import.meta.url).pathname);
const Port = Number(process.env.PORT || 5174);
const MimeTypes = {
    '.html': 'text/html; charset=utf-8',
    '.js': 'text/javascript; charset=utf-8',
    '.mjs': 'text/javascript; charset=utf-8',
    '.css': 'text/css; charset=utf-8',
    '.json': 'application/json; charset=utf-8',
    '.png': 'image/png',
    '.svg': 'image/svg+xml',
    '.md': 'text/markdown; charset=utf-8'
};

const Server = createServer(async (Request, Response) =>
{
    try
    {
        const Pathname = decodeURIComponent(new URL(Request.url, 'http://localhost').pathname);
        const Relative = Pathname === '/' ? 'index.html' : Pathname.replace(/^\/+/, '');
        const Candidate = resolve(join(RootDirectory, normalize(Relative)));
        if (Candidate !== RootDirectory && !Candidate.startsWith(RootDirectory + sep))
        {
            Response.writeHead(403).end('Forbidden');
            return;
        }
        const Information = await stat(Candidate);
        if (!Information.isFile()) throw new Error('Not a file');
        const Bytes = await readFile(Candidate);
        Response.writeHead(200, {
            'Content-Type': MimeTypes[extname(Candidate)] || 'application/octet-stream',
            'Cache-Control': 'no-store',
            'Access-Control-Allow-Origin': '*'
        });
        Response.end(Bytes);
    }
    catch
    {
        Response.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' }).end('Not found');
    }
});

Server.listen(Port, '0.0.0.0', () =>
{
    console.log(`World Forge editor: http://0.0.0.0:${Port}/`);
});
