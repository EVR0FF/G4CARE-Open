import { defineConfig } from 'vite';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  server: {
    // In development the API is proxied to the backend on :3000.
    proxy: {
      '/artifacts': 'http://127.0.0.1:3000',
      '/auth': 'http://127.0.0.1:3000',
      '/health': 'http://127.0.0.1:3000',
      '/folders': 'http://127.0.0.1:3000',
      '/trash': 'http://127.0.0.1:3000',
      '/share': 'http://127.0.0.1:3000',
    },
  },
});
