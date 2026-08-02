#ifndef __CR_PAGEMAP_REGION_H__
#define __CR_PAGEMAP_REGION_H__

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

/*
 * Memory region payload layout describing how pagemap entries or I/O vectors
 * are chunked and stored in pages-*.img.
 */
struct page_region_layout {
	uint32_t *sizes;        /* Payload byte size per block in the image */
	size_t nr_blocks;       /* Total number of blocks (pages or regions) */
	uint64_t total_bytes;   /* Sum of all block payload sizes */
	uint32_t pages_per_reg; /* Granularity: virtual pages per block (1 for page-sized regions) */
};

/*
 * Helper to round a page count down to the nearest multiple of the region granularity.
 * Used when reading bounded memory batches without splitting a region across chunks.
 */
static inline unsigned long region_align_down(unsigned long nr_pages, uint32_t pages_per_reg)
{
	if (pages_per_reg <= 1)
		return nr_pages;
	return nr_pages - (nr_pages % pages_per_reg);
}

/*
 * Helper to calculate the number of blocks required to cover @nr_pages
 * given a specific block granularity (@pages_per_reg).
 */
static inline unsigned long region_nr_blocks(unsigned long nr_pages, uint32_t pages_per_reg)
{
	if (pages_per_reg <= 1)
		return nr_pages;
	return (nr_pages + pages_per_reg - 1) / pages_per_reg;
}

#endif /* __CR_PAGEMAP_REGION_H__ */
