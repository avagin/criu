#ifndef __CR_PAGE_XFER__H__
#define __CR_PAGE_XFER__H__
#include "pagemap.h"

extern int cr_page_server(bool daemon_mode, int cfd);

/*
 * page_xfer -- transfer pages into image file.
 * Two images backends are implemented -- local image file
 * and page-server image file.
 */

struct page_xfer {
	/* transfers one vaddr:len entry */
	int (*write_pagemap)(struct page_xfer *self, struct iovec *iov, u32 flags);
	/* transfers pages related to previous pagemap */
	int (*write_pages)(struct page_xfer *self, int pipe, unsigned long len);
	void (*close)(struct page_xfer *self);

	/* private data for every page-xfer engine */
	union {
		struct /* local */ {
			struct cr_img *pmi; /* pagemaps */
			struct cr_img *pi;  /* pages */
		};

		struct /* page-server */ {
			int sk;
			u64 dst_id;
		};
	};

	struct page_read *parent;
};

extern int open_page_xfer(struct page_xfer *xfer, int fd_type, long id);
struct page_pipe;
extern int page_xfer_dump_pages(struct page_xfer *, struct page_pipe *,
				unsigned long off, bool dump_lazy);
extern int connect_to_page_server(void);
extern int disconnect_from_page_server(void);

extern int check_parent_page_xfer(int fd_type, long id);

extern int get_remote_pages(int pid, unsigned long addr, int nr_pages, void *dest);

#endif /* __CR_PAGE_XFER__H__ */
