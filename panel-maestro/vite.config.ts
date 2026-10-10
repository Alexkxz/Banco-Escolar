import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

export default defineConfig(({ mode }) => ({
  base: mode === 'sd' ? '/panel-test/maestro/' : '/',
  plugins: [react()],
  build: mode === 'sd' ? {
    outDir: 'dist-sd',
    emptyOutDir: true,
    manifest: true,
  } : undefined,
  test: {
    environment: 'jsdom',
    setupFiles: './tests/setup.ts',
    css: true,
  },
}))
