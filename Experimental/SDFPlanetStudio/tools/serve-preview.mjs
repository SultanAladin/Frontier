/**
 * Public preview server.
 *
 * Why this exists: the sandbox preview sits behind a proxy that is free to cache.
 * Vite's dev server serves unbusted module URLs (`/src/app/macro-viewport.js`), so an
 * older copy of that file could be executed after it had already been fixed — which is
 * exactly what happened once: the browser threw on a line that no longer existed in the
 * source. Serving the built bundle instead makes staleness impossible for the app
 * itself, because every asset filename is content-hashed.
 *
 * Routing:
 *   /assets/*           hashed build output, cached forever
 *   /, /index.html      dist/index.html, never cached
 *   other dist files    served from dist
 *   everything else     proxied to the internal dev server (127.0.0.1:5174) so that an
 *                       old cached dev-mode HTML still finds /src/**, /@vite/** and
 *                       /node_modules/** instead of 404-ing into a blank page
 */

import http from 'node:http';
import { createReadStream } from 'node:fs';
import { stat } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const ROOT = path.dirname(path.dirname(fileURLToPath(import.meta.url)));
const DIST = path.join(ROOT, 'dist');
const PORT = Number(process.env.PORT ?? 5173);
const DEV = { host: '127.0.0.1', port: Number(process.env.DEV_PORT ?? 5174) };

const MIME = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.mjs': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.json': 'application/json; charset=utf-8',
  '.map': 'application/json; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp',
  '.ico': 'image/x-icon',
  '.woff': 'font/woff',
  '.woff2': 'font/woff2',
  '.wasm': 'application/wasm',
};

const isFile = async (target) => {
  try {
    return (await stat(target)).isFile();
  } catch {
    return false;
  }
};

function sendFile(request, response, file, cacheControl) {
  const type = MIME[path.extname(file).toLowerCase()] ?? 'application/octet-stream';
  response.writeHead(200, {
    'content-type': type,
    'cache-control': cacheControl,
    'access-control-allow-origin': '*',
  });
  if (request.method === 'HEAD') {
    response.end();
    return;
  }
  createReadStream(file).pipe(response);
}

function proxyToDev(request, response) {
  const upstream = http.request(
    {
      host: DEV.host,
      port: DEV.port,
      path: request.url,
      method: request.method,
      headers: { ...request.headers, host: `${DEV.host}:${DEV.port}` },
    },
    (upstreamResponse) => {
      const headers = { ...upstreamResponse.headers };
      // Only the hashed build output may be cached; sources must always revalidate.
      if (!request.url?.startsWith('/assets/')) headers['cache-control'] = 'no-store';
      response.writeHead(upstreamResponse.statusCode ?? 502, headers);
      upstreamResponse.pipe(response);
    },
  );

  upstream.on('error', (error) => {
    response.writeHead(503, { 'content-type': 'text/plain; charset=utf-8', 'cache-control': 'no-store' });
    response.end(`Dev server unavailable on ${DEV.host}:${DEV.port} (${error.code ?? error.message}).\n`);
  });

  request.pipe(upstream);
}

const server = http.createServer(async (request, response) => {
  const url = new URL(request.url ?? '/', 'http://localhost');
  const pathname = decodeURIComponent(url.pathname);

  if (pathname === '/' || pathname === '/index.html') {
    const index = path.join(DIST, 'index.html');
    if (await isFile(index)) {
      sendFile(request, response, index, 'no-store');
      return;
    }
    proxyToDev(request, response);
    return;
  }

  // Never let a traversal escape dist.
  const relative = path.normalize(pathname).replace(/^(\.\.[/\\])+/, '').replace(/^[/\\]+/, '');
  const target = path.join(DIST, relative);
  if (target.startsWith(DIST) && (await isFile(target))) {
    const cacheControl = pathname.startsWith('/assets/')
      ? 'public, max-age=31536000, immutable'
      : 'no-store';
    sendFile(request, response, target, cacheControl);
    return;
  }

  proxyToDev(request, response);
});

server.listen(PORT, '0.0.0.0', () => {
  console.log(`[preview] serving dist + dev proxy on http://0.0.0.0:${PORT}`);
});
