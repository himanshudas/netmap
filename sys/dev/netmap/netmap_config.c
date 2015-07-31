/*
 * Copyright (C) 2014 Giuseppe Lettieri. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* $FreeBSD: readp/sys/dev/netmap/netmap_pipe.c 261909 2014-02-15 04:53:04Z luigi $ */

#if defined(__FreeBSD__)
#include <sys/cdefs.h> /* prerequisite */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/param.h>	/* defines used in kernel.h */
#include <sys/kernel.h>	/* types used in module initialization */
#include <sys/malloc.h>
#include <sys/poll.h>
#include <sys/lock.h>
#include <sys/rwlock.h>
#include <sys/selinfo.h>
#include <sys/sysctl.h>
#include <sys/socket.h> /* sockaddrs */
#include <net/if.h>
#include <net/if_var.h>
#include <machine/bus.h>	/* bus_dmamap_* */
#include <sys/refcount.h>


#elif defined(linux)

#include "bsd_glue.h"

#elif defined(__APPLE__)

#warning OSX support is only partial
#include "osx_glue.h"

#else

#error	Unsupported platform

#endif /* unsupported */

/*
 * common headers
 */

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>
#include <dev/netmap/netmap_mem2.h>

#ifdef WITH_NMCONF

#define NM_CBDATASIZ 1024
#define NM_CBDATAMAX 4

/* simple buffers for incoming/outgoing data on read()/write() */

struct nm_confbuf_data {
	struct nm_confbuf_data *chain;
	u_int size;
	char data[];
};

void*
netmap_confbuf_pre_write(struct netmap_confbuf *cb, u_int req_size, u_int *avl_size)
{
	struct nm_confbuf_data *d, *nd;
	u_int s = 0;
	void *ret;

	d = cb->writep;
	/* get the current available space */
	if (d)
		s = d->size - cb->next_w;
	if (s > 0 && (s >= req_size || avl_size))
		goto out;
	/* we need to expand the buffer, if possible */
	if (cb->n_data >= NM_CBDATAMAX)
		return NULL;
	s = NM_CBDATASIZ;
	if (req_size > s && avl_size == NULL)
		s = req_size;
	nd = malloc(sizeof(*d) + s, M_DEVBUF, M_NOWAIT);
	if (nd == NULL)
		return NULL;
	nd->size = s;
	if (d) {
		/* the caller is not willing to do a short write
		 * and the available space in the current chunk
		 * is not big enough. Truncate the chunk and
		 * move to the next one.
		 */
		d->size = cb->next_w;
		d->chain = nd;
	}
	cb->writep = nd;
	cb->next_w = 0;
	cb->n_data++;
	if (cb->readp == NULL) {
		/* this was the first chunk, 
		 * initialize the read pointer
		 */
		cb->readp = cb->writep;
	}
	d = nd;
out:
	if (s > req_size)
		s = req_size;
	if (avl_size)
		*avl_size = s;
	ret = d->data + cb->next_w;
	cb->next_w += s;
	return ret;
}

void*
netmap_confbuf_pre_read(struct netmap_confbuf *cb, u_int *size)
{
	struct nm_confbuf_data *d;

	for (;;) {
		d = cb->readp;
		if (d == NULL) {
			*size = 0;
			return NULL;
		}
		if (d->size > cb->next_r) {
			/* there is something left to read
			 * in this chunk
			 */
			u_int s = d->size - cb->next_r;
			void *ret = d->data + cb->next_r;
			if (*size < s)
				s = *size;
			else
				*size = s;
			cb->next_r += s;
			return ret;
		}
		/* chunk exausted, move to the next one */
		cb->readp = d->chain;
		cb->next_r = 0;
		free(d, M_DEVBUF);
		cb->n_data--;
	}
}

int
netmap_confbuf_getc(struct netmap_confbuf *cb)
{
	u_int size = 1;
	void *c = netmap_confbuf_pre_read(cb, &size);
	if (c)
		return *(char*)c;
	return 0;
}

void
netmap_confbuf_destroy(struct netmap_confbuf *cb)
{
	struct nm_confbuf_data *d = cb->readp;

	while (d) {
		struct nm_confbuf_data *nd = d->chain;
		free(d, M_DEVBUF);
		d = nd;
	}
	memset(cb, 0, sizeof(*cb));
}

void
netmap_config_init(struct netmap_config *c)
{
	NM_MTX_INIT(c->mux);
}

void
netmap_config_uninit(struct netmap_config *c)
{
	int i;
	
	netmap_config_parse(c);
	for (i = 0; i < 2; i++)
		netmap_confbuf_destroy(c->buf + i);
	NM_MTX_DESTROY(c->mux);
}

void
netmap_config_parse(struct netmap_config *c)
{
}

#endif /* WITH_NMCONF */
