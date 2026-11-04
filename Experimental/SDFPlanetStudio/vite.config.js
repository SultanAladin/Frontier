import { defineConfig } from 'vite';

// Stamp baked into the bundle so a stale page can be identified from the console or
// the header chip instead of by guessing.
const BUILD_STAMP = new Date().toISOString().replace(/[-:]/g, '').replace('T', '-').slice(0, 15);

export default defineConfig({
  define: { __BUILD_STAMP__: JSON.stringify(BUILD_STAMP) },

  // The dev server is internal now: it exists to serve the sources that
  // tools/serve-preview.mjs proxies, and to keep HMR available while editing.
  server: {
    host: '127.0.0.1',
    port: 5174,
    strictPort: true,
    allowedHosts: true,
    cors: true,
    headers: { 'Cache-Control': 'no-store' },
  },

  // Public entry point: content-hashed assets, so an updated build can never be
  // shadowed by a cached module from an older one.
  preview: {
    host: '0.0.0.0',
    port: 5173,
    strictPort: true,
    allowedHosts: true,
    cors: true,
    headers: { 'Cache-Control': 'no-store' },
  },

  worker: { format: 'es' },
  build: { target: 'es2022' },
});
