/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#ifndef _VARCHUNK_H
#define _VARCHUNK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>

#if !defined(_WIN32)
#	include <sys/mman.h> // mlock
#endif

/*****************************************************************************
 * API START
 *****************************************************************************/

typedef struct _varchunk_t varchunk_t;

static inline int
varchunk_is_lock_free(void);

static inline varchunk_t *
varchunk_new(size_t minimum);

static inline void
varchunk_free(varchunk_t *varchunk);

static inline void *
varchunk_write_request(varchunk_t *varchunk, size_t minimum);

static inline void
varchunk_write_advance(varchunk_t *varchunk, size_t written);

static inline const void *
varchunk_read_request(varchunk_t *varchunk, size_t *toread);

static inline void
varchunk_read_advance(varchunk_t *varchunk);

/*****************************************************************************
 * API END
 *****************************************************************************/

#define VARCHUNK_PAD(SIZE) ( ( (size_t)(SIZE) + 7U ) & ( ~7U ) )

typedef struct _varchunk_elmnt_t varchunk_elmnt_t;

struct _varchunk_elmnt_t {
	uint32_t size;
	uint32_t gap;	
};

struct _varchunk_t {
  size_t size;
  size_t mask;
	size_t rsvd;

  _Atomic size_t head;
  _Atomic size_t tail;

  void *buf;
}; 

static inline int
varchunk_is_lock_free(void)
{
	varchunk_t varchunk;

	return atomic_is_lock_free(&varchunk.head)
	 && atomic_is_lock_free(&varchunk.tail);
}

static inline varchunk_t *
varchunk_new(size_t minimum)
{
	varchunk_t *varchunk;
	
	if(!(varchunk = calloc(1, sizeof(varchunk_t))))
		return NULL;

	atomic_init(&varchunk->head, 0);
	atomic_init(&varchunk->tail, 0);
	
	varchunk->size = 1;
	while(varchunk->size < minimum)
		varchunk->size <<= 1; // assure size to be a power of 2
	varchunk->mask = varchunk->size - 1;

#if defined(_WIN32)
	varchunk->buf = _aligned_malloc(varchunk->size, sizeof(varchunk_elmnt_t));
#else
	posix_memalign(&varchunk->buf, sizeof(varchunk_elmnt_t), varchunk->size);
	mlock(varchunk->buf, varchunk->size); // prevent memory from being flushed to disk
#endif
	if(!varchunk->buf)
	{
		free(varchunk);
		return NULL;
	}
	
	return varchunk;
}

static inline void
varchunk_free(varchunk_t *varchunk)
{
	if(varchunk)
	{
		if(varchunk->buf)
		{
#if !defined(_WIN32)
			munlock(varchunk->buf, varchunk->size);
#endif
			free(varchunk->buf);
		}
		free(varchunk);
	}
}

static inline void
_varchunk_write_advance_raw(varchunk_t *varchunk, size_t head, size_t written)
{
	// only producer is allowed to advance write head
	size_t new_head = (head + written) & varchunk->mask;
	atomic_store_explicit(&varchunk->head, new_head, memory_order_release);
}

static inline void *
varchunk_write_request(varchunk_t *varchunk, size_t minimum)
{
	if(minimum == 0)
	{
		varchunk->rsvd = 0;
		return NULL;
	}

	size_t space; // size of writable buffer
	size_t end; // virtual end of writable buffer
	size_t head = atomic_load_explicit(&varchunk->head, memory_order_relaxed); // read head
	size_t tail = atomic_load_explicit(&varchunk->tail, memory_order_acquire); // read tail (consumer modifies it any time)
	size_t padded = 2*sizeof(varchunk_elmnt_t) + VARCHUNK_PAD(minimum);

	// calculate writable space
	if(head > tail)
		space = ((tail - head + varchunk->size) & varchunk->mask) - 1;
	else if(head < tail)
		space = (tail - head) - 1;
	else // head == tail
		space = varchunk->size - 1;
	end = head + space;

	if(end > varchunk->size) // available region wraps over at end of buffer
	{
		// get first part of available buffer
		void *buf1 = varchunk->buf + head;
		size_t len1 = varchunk->size - head;

		if(len1 < padded) // not enough space left on first part of buffer
		{
			// get second part of available buffer
			void *buf2 = varchunk->buf;
			size_t len2 = end & varchunk->mask;

			if(len2 < padded) // not enough space left on second buffer, either
			{
				varchunk->rsvd = 0;
				return NULL;
			}
			else // enough space left on second buffer, use it!
			{
				// fill end of first buffer with gap
				varchunk_elmnt_t *elmnt = buf1;
				elmnt->size = len1 - sizeof(varchunk_elmnt_t);
				elmnt->gap = 1;
				_varchunk_write_advance_raw(varchunk, head, len1);

				varchunk->rsvd = minimum;
				return buf2 + sizeof(varchunk_elmnt_t);
			}
		}
		else // enough space left on first part of buffer, use it!
		{
			varchunk->rsvd = minimum;
			return buf1 + sizeof(varchunk_elmnt_t);
		}
	}
	else // available region is contiguous
	{
		void *buf = varchunk->buf + head;

		if(space < padded) // no space left on contiguous buffer
		{
			varchunk->rsvd = 0;
			return NULL;
		}
		else // enough space left on contiguous buffer, use it!
		{
			varchunk->rsvd = minimum;
			return buf + sizeof(varchunk_elmnt_t);
		}
	}
}

static inline void
varchunk_write_advance(varchunk_t *varchunk, size_t written)
{
	// fail miserably if stupid programmer tries to write more than rsvd
	assert(written <= varchunk->rsvd);

	// write elmnt header at head
	size_t head = atomic_load_explicit(&varchunk->head, memory_order_relaxed);
	varchunk_elmnt_t *elmnt = varchunk->buf + head;
	elmnt->size = written;
	elmnt->gap = 0;

	// advance write head
	_varchunk_write_advance_raw(varchunk, head,
		sizeof(varchunk_elmnt_t) + VARCHUNK_PAD(written));
}

static inline void
_varchunk_read_advance_raw(varchunk_t *varchunk, size_t tail, size_t read)
{
	// only consumer is allowed to advance read tail 
	size_t new_tail = (tail + read) & varchunk->mask;
	atomic_store_explicit(&varchunk->tail, new_tail, memory_order_release);
}

static inline const void *
varchunk_read_request(varchunk_t *varchunk, size_t *toread)
{
	size_t space; // size of available buffer
	size_t tail = atomic_load_explicit(&varchunk->tail, memory_order_relaxed); // read tail
	size_t head = atomic_load_explicit(&varchunk->head, memory_order_acquire); // read head (producer modifies it any time)

	// calculate readable space
	if(head > tail)
		space = head - tail;
	else
		space = (head - tail + varchunk->size) & varchunk->mask;

	if(space > 0) // there may be chunks available for reading
	{
		size_t end = tail + space; // virtual end of available buffer

		if(end > varchunk->size) // available buffer wraps around at end
		{
			// first part of available buffer
			void *buf1 = varchunk->buf + tail;
			size_t len1 = varchunk->size - tail;
			const varchunk_elmnt_t *elmnt = buf1;

			if(elmnt->gap) // gap elmnt?
			{
				// skip gap
				_varchunk_read_advance_raw(varchunk, tail, len1);

				// second part of available buffer
				void *buf2 = varchunk->buf;
				elmnt = buf2;

				*toread = elmnt->size;
				return buf2 + sizeof(varchunk_elmnt_t);
			}
			else // valid chunk, use it!
			{
				*toread = elmnt->size;
				return buf1 + sizeof(varchunk_elmnt_t);
			}
		}
		else // available buffer is contiguous
		{
			// get buffer
			void *buf = varchunk->buf + tail;
			const varchunk_elmnt_t *elmnt = buf;

			if(elmnt->gap) // a single gap elmnt?
			{
				// skip gap
				_varchunk_read_advance_raw(varchunk, tail, space);

				*toread = 0;
				return NULL;
			}
			else // valid chunk, use it!
			{
				*toread = elmnt->size;
				return buf + sizeof(varchunk_elmnt_t);
			}
		}
	}
	else // no chunks available aka empty buffer
	{
		*toread = 0;
		return NULL;
	}
}

static inline void
varchunk_read_advance(varchunk_t *varchunk)
{
	// get elmnt header from tail (for size)
	size_t tail = atomic_load_explicit(&varchunk->tail, memory_order_relaxed);
	const varchunk_elmnt_t *elmnt = varchunk->buf + tail;

	// advance read tail
	_varchunk_read_advance_raw(varchunk, tail,
		sizeof(varchunk_elmnt_t) + VARCHUNK_PAD(elmnt->size));
}

#undef VARCHUNK_PAD

#ifdef __cplusplus
}
#endif

#endif //_VARCHUNK_H
