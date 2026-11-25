import { defineConfig, type Plugin, type ProxyOptions } from 'vite'

// Forward embeds the pages as one Qt resource, so the build inlines its script and styles into
// index.html. Fonts come from Forward's own resources at /fonts/.
function singleFile(): Plugin {
  const tag = (name: string, pattern: string) => new RegExp(pattern.replace('NAME', name.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')))
  return {
    name: 'single-file',
    enforce: 'post',
    generateBundle(_options, bundle) {
      const page = bundle['index.html']
      if (page?.type !== 'asset') return
      let html = String(page.source)
      for (const [name, output] of Object.entries(bundle)) {
        if (output.type === 'chunk') {
          html = html.replace(tag(name, '<script[^>]*src="[^"]*NAME"[^>]*></script>'), () => `<script type="module">${output.code}</script>`)
          delete bundle[name]
        } else if (name.endsWith('.css')) {
          html = html.replace(tag(name, '<link[^>]*href="[^"]*NAME"[^>]*>'), () => `<style>${output.source}</style>`)
          delete bundle[name]
        }
      }
      page.source = html
    },
  }
}

// `npm run dev` serves the pages with hot reload against a running Forward.
const forward: ProxyOptions = {
  target: 'http://127.0.0.1:18744',
  // Forward only answers its own origin.
  configure: proxy => proxy.on('proxyReq', request => request.removeHeader('origin')),
}

export default defineConfig({
  plugins: [singleFile()],
  server: { proxy: { '/api/app': forward, '/fonts': forward } },
  build: { cssCodeSplit: false, modulePreload: false, assetsInlineLimit: Number.MAX_SAFE_INTEGER },
})
