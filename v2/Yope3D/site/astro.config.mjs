// @ts-check
import { defineConfig } from 'astro/config';

// GitHub Pages project-site config. When yope3d.com is wired up as the
// custom domain, switch to: site: 'https://yope3d.com', base: '/'.
export default defineConfig({
  site: 'https://yugumishra.github.io',
  base: '/Yope3D',
  markdown: {
    // Dark-only site; theme bg (#0d1117) sits close to the site bg (#0b0d10).
    shikiConfig: {
      theme: 'github-dark-default',
    },
  },
});
