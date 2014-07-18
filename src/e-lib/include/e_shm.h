/*
  File: e_shm.h

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2013 Adapteva, Inc.
  See AUTHORS for list of contributors.
  Support e-mail: <support@adapteva.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License (LGPL)
  as published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  and the GNU Lesser General Public License along with this program,
  see the files COPYING and COPYING.LESSER.	 If not, see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _E_SHM_H_
#define _E_SHM_H_

#define MAX_SHM_REGIONS				   64

#include <sys/types.h>
#include "e_common.h"

#pragma pack(push, 1)

/**
 * The following types must be defined identically to those in
 * parallella-linux-adi/include<uapi/epiphany.h>
 *
 * FIXME: Find a way to #include the epiphany.h header from the kernel tree
 * in the parallella-linux-adi git repository.
 */

/** Shared memory segment */
typedef struct ALIGN(8) e_shmseg {
	void	 *addr;			   /* Virtual address */
	char	  name[256];	   /* Region name */
	size_t	  size;			   /* Region size in bytes */
	void	 *paddr;		   /* Physical Address accessible from Epiphany cores */
	off_t	  offset;		   /* Offset from shm base address */
} e_shmseg_t;

typedef struct ALIGN(8) e_shmseg_pvt {
	e_shmseg_t	shm_seg;  /* The shared memory segment */
	unsigned	refcnt;	  /* host app reference count */
	unsigned	valid;	  /* 1 if the region is in use, 0 otherwise */
} e_shmseg_pvt_t;

typedef struct ALIGN(8) e_shmtable {
	unsigned int   magic;
	unsigned int   padding;
	e_shmseg_pvt_t regions[MAX_SHM_REGIONS];
	unsigned int   free_space;
	off_t		   next_free_offset;
	unsigned long  paddr_epi;	/* Physical address of the shm region as seen by epiphany */
	unsigned long  paddr_cpu;	/* Physical address of the shm region as seen by the host cpu */
	char*		   heap;
	void*		   lock;		/* User-space semaphore */
} e_shmtable_t;

#pragma pack(pop)

/** Attach to a shared region identifiable by name */
const e_shmseg_t* e_shm_attach(const char* const name);

/** Release a shared region allocated with e_shm_attach() */
int e_shm_release(const char* name);

#endif	  /* _E_SHM_H_ */