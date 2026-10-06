import { defineConfig } from 'vitest/config';
import react from '@vitejs/plugin-react';

export default defineConfig({
  plugins: [react()],
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./src/test/setup.ts'],
    server: {
      deps: {
        // antd v5 ships ESM under a CJS wrapper — inline it for Vitest.
        inline: ['@ant-design/icons', '@ant-design/colors', '@ant-design/cssinjs', /antd/, /^rc-/],
      },
    },
  },
});
