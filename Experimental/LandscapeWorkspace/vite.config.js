/*==========================================================================================================================================
                                                             VITE.CONFIG.JS
==========================================================================================================================================*/
// 🧩 Local prototype hosting with preview-origin support and access to the existing shared font reference.

import { defineConfig } from 'vite';
import { fileURLToPath } from 'node:url';

export default defineConfig({
    base: './',
    server: {
        host: '0.0.0.0',
        allowedHosts: ['.e2b.app'],
        fs: { allow: [fileURLToPath(new URL('../../', import.meta.url))] }
    },
    preview: { host: '0.0.0.0', allowedHosts: ['.e2b.app'] }
});
