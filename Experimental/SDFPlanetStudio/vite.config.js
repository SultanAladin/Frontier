import { defineConfig } from 'vite';

// The preview proxy terminates TLS in front of these ports, so the dev server must
// accept the proxied Host header and serve from 0.0.0.0.
export default defineConfig({
  server: {
    host: '0.0.0.0',
    port: 5173,
    strictPort: true,
    allowedHosts: true,
    cors: true,
  },
  worker: {
    format: 'es',
  },
  build: {
    target: 'es2022',
  },
});
