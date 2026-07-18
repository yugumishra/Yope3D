# Yope3D site

Astro static site for [yugumishra.github.io/Yope3D](https://yugumishra.github.io/Yope3D) — landing page + devlog. Deployed automatically by `.github/workflows/deploy-site.yml` (repo root) on any push to `main` touching `v2/Yope3D/site/**`.

## Commands

| Command           | Action                                      |
| :---------------- | :------------------------------------------ |
| `npm install`     | Install dependencies                        |
| `npm run dev`     | Dev server at `localhost:4321/Yope3D`       |
| `npm run build`   | Production build to `./dist/`               |
| `npm run preview` | Preview the production build locally        |

## Adding a devlog post

1. Create `src/content/blog/<slug>.md`. The filename becomes the URL: `/Yope3D/blog/<slug>/`.
2. Frontmatter (schema in `src/content.config.ts`):

   ```yaml
   ---
   title: 'Article Title'
   description: One-to-two sentence summary (used in the listing and meta description).
   date: 2026-07-18
   cover: ./images/my-cover.png   # optional hero image
   coverAlt: Alt text             # optional
   draft: true                    # visible in dev only; flip to false (or delete) to publish
   ---
   ```

3. Write markdown. Everything below is styled by `src/pages/blog/[...slug].astro`:
   - **Headings**: start at `##` (`#` is reserved for the frontmatter title).
   - **Code blocks**: fenced with a language tag → Shiki highlighting (`github-dark-default`).
   - **Images**: co-locate under `src/content/blog/images/` and reference relatively (`![alt](./images/foo.png)`) — optimized + content-hashed + base-path-aware. Raw HTML `<img>` tags are *not* processed; for captions, put a markdown image inside `<figure>` with blank lines around it.
   - **Video embeds**: a bare `<iframe src="https://www.youtube-nocookie.com/embed/<id>" ...>` is styled responsive full-width. Self-hosted files go in `public/videos/` and need the explicit `/Yope3D/` prefix in `<video src>`.
   - **Tables, blockquotes, hr**: standard markdown, styled.

4. Drafts (`draft: true`) render in `astro dev` with a badge and are excluded from production builds — `src/content/blog/sample-formatting-reference.md` is a permanent draft that demonstrates every feature.

At publish time (per `articlePlan.txt`): tag the commit `article/NN-slug`.

## Base path

The site deploys under `/Yope3D` (see `astro.config.mjs`). Internal links in `.astro` files use `import.meta.env.BASE_URL`; markdown-referenced images handle it automatically; only hand-written `public/` asset URLs need the prefix spelled out. When the custom domain lands, switch `site`/`base` in the config and fix those `public/` references.
