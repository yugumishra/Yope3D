import { defineCollection, z } from 'astro:content';
import { glob } from 'astro/loaders';

const blog = defineCollection({
	loader: glob({ pattern: '**/*.md', base: './src/content/blog' }),
	schema: ({ image }) =>
		z.object({
			title: z.string(),
			description: z.string(),
			date: z.coerce.date(),
			updated: z.coerce.date().optional(),
			// Optional hero image shown between the header and the body.
			cover: image().optional(),
			coverAlt: z.string().optional(),
			// Drafts render in `astro dev` but are excluded from production builds.
			draft: z.boolean().default(false),
		}),
});

export const collections = { blog };
