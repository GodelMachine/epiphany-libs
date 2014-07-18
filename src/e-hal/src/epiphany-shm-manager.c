/*
  File: epiphany-shm-manager.c

  This file is part of the Epiphany Software Development Kit.

  Copyright (C) 2014 Adapteva, Inc.
  See AUTHORS for list of contributors
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

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <err.h>
#include <assert.h>
#include <stdio.h>
#include <semaphore.h>

#include "epiphany-hal.h"
#include "epiphany-hal-api-local.h"
#include "epiphany-shm-manager.h"

static e_shmtable_t *shm_table = 0;
static size_t shm_table_length = 0;

static e_shmseg_pvt_t* shm_lookup_region(const char *name);
static e_shmseg_pvt_t* shm_alloc_region(const char *name, size_t size);
static void shm_compact_heap(void);

extern int	 e_host_verbose;
#define diag(vN) if (e_host_verbose >= vN)

/**
 * Initialize the shared memory manager.
 */
int e_shm_init()
{
	epiphany_alloc_t shm_alloc;
	int devfd = 0;
	const unsigned sem_perms = S_IRUSR | S_IWUSR;

	/* Map the epiphany global shared memory into process address space */
	devfd = open(EPIPHANY_DEV, O_RDWR | O_SYNC);
	if ( -1 == devfd ) {
		warnx("e_init(): EPIPHANY_DEV file open failure.");
		return E_ERR;
	}

	memset(&shm_alloc, 0, sizeof(shm_alloc));
	if ( -1 == ioctl(devfd, EPIPHANY_IOC_GETSHM, &shm_alloc) ) {
		warnx("e_shm_init(): Failed to obtain the global "
			  "shared memory. Error is %s\n", strerror(errno));
		return E_ERR;
	}
	shm_table_length = shm_alloc.size;

	shm_alloc.uvirt_addr = (unsigned long)mmap(0, shm_alloc.size,
		PROT_READ|PROT_WRITE, MAP_SHARED, devfd, (off_t)shm_alloc.mmap_handle);
	if ( MAP_FAILED == (void*)shm_alloc.uvirt_addr ) {
		warnx("e_shm_init(): Failed to map global shared memory. Error is %s\n",
			  strerror(errno));
		return E_ERR;
	}

	diag(H_D2) { fprintf(stderr, "e_shm_init(): mapped shm: handle 0x%08lx, "
						 "uvirt 0x%08lx, size 0x%08lx\n", shm_alloc.mmap_handle,
						 shm_alloc.uvirt_addr, shm_alloc.size); }

	/** The shm table is initialized by the Epiphany driver. */
	shm_table = (e_shmtable_t*)shm_alloc.uvirt_addr;

	diag(H_D2) { fprintf(stderr, "e_shm_init(): shm table size is 0x%08x\n",
						 sizeof(e_shmtable_t)); }
	

	if ( shm_table->magic != SHM_MAGIC ) {
		warnx("e_shm_init(): Bad shm magic. Expected 0x%08x found 0x%08x\n",
			  SHM_MAGIC, shm_table->magic);
		return E_ERR;
	}

	/* Init the shm lock semaphore. Init unlocked */
	shm_table->lock = sem_open(SHM_LOCK_NAME, O_CREAT, sem_perms, 1);
	if ( SEM_FAILED == (sem_t*)shm_table->lock ) {
		warnx("e_shm_init(): Failed to open the shared memory semaphore. "
			  "Error is %s\n", strerror(errno));
		return E_ERR;
	}

	diag(H_D2) { fprintf(stderr, "e_shm_init(): initialization complete\n"); }

	return E_OK;
}

void e_shm_finalize(void)
{
	sem_unlink(SHM_LOCK_NAME);
	sem_close(shm_table->lock);
	munmap((void*)shm_table, shm_table_length);
	diag(H_D2) { fprintf(stderr, "e_shm_finalize(): teardown complete\n"); }
}

e_shmseg_t* e_shm_alloc(const char *name, size_t size)
{
	e_shmtable_t   *tbl	   = NULL;
	e_shmseg_pvt_t *region = NULL; 
	e_shmseg_t	   *retval = NULL;

	if ( !name || !size ) {
		errno = EINVAL;
		goto err2;
	}

	tbl = e_shm_get_shmtable();

	if ( !tbl ) {
		assert(!"Global shm table is NULL, did you forget to call"
			   " e_shm_init()?");
		errno = EINVAL;
		goto err2;
	}

	sem_wait(tbl->lock);

	if ( shm_lookup_region(name) ) {
		errno = EEXIST;
		goto err1;
	}

	if ( size > shm_table->free_space ) {
		shm_compact_heap();
		
		if ( size > shm_table->free_space ) {
			errno = ENOMEM;
			goto err1;
		}
	}

	region = shm_alloc_region(name, size);
	if ( region ) {
		region->valid = 1;
		region->refcnt = 1;
		retval = &region->shm_seg;
	}

 err1:	  
	sem_post(shm_table->lock);

 err2:
	return retval;
}

e_shmseg_t* e_shm_attach(const char *name)
{
	e_shmtable_t   *tbl	   = NULL;
	e_shmseg_pvt_t *region = NULL;
	e_shmseg_t	   *retval = NULL;

	tbl = e_shm_get_shmtable();
	sem_wait(tbl->lock);
	region = shm_lookup_region(name);
	if ( region ) {
		++region->refcnt;
		retval = &region->shm_seg;
	}
	sem_post(tbl->lock);

	return retval;
}

int e_shm_release(const char *name)
{
	e_shmtable_t	 *tbl	 = NULL;
	e_shmseg_pvt_t	 *region = NULL;
	int				  retval = E_ERR;

	tbl = e_shm_get_shmtable();
	sem_wait(tbl->lock);

	region = shm_lookup_region(name);

	if ( region ) {
		if ( 0 == --region->refcnt ) {
			tbl->free_space -= region->shm_seg.size;
			region->valid = 0;
		}
		retval = E_OK;
	}

	sem_post(tbl->lock);
	return retval;
}

e_shmtable_t* e_shm_get_shmtable(void)
{
	return shm_table;
}

/**
 * Search the shm table for a region named by name.
 *
 * WARNING: The caller should hold the shm table lock when
 * calling this function.
 */
static e_shmseg_pvt_t*
shm_lookup_region(const char *name)
{
	e_shmseg_pvt_t	 *retval = NULL;
	e_shmtable_t	 *tbl	 = NULL;
	int				  i		 = 0;

	tbl = e_shm_get_shmtable();
	
	for ( i = 0; i < MAX_SHM_REGIONS; ++i ) {
		if ( tbl->regions[i].valid && 
			 !strcmp(name, tbl->regions[i].shm_seg.name) ) {
			retval = &tbl->regions[i];
			break;
		}
	}

	return retval;
}

/**
 * Search the shm table for a region named by name.
 *
 * WARNING: The caller should hold the shm table lock when
 * calling this function.
 */
static e_shmseg_pvt_t*
shm_alloc_region(const char *name, size_t size)
{
	e_shmseg_pvt_t	 *region = NULL;
	e_shmtable_t	 *tbl	 = NULL;
	int				  i		 = 0;

	tbl = e_shm_get_shmtable();

	for ( i = 0; i < MAX_SHM_REGIONS; ++i ) {
		if ( !tbl->regions[i].valid ) {
			region = &tbl->regions[i];
			strncpy(region->shm_seg.name, name, sizeof(region->shm_seg.name));

			/*
			 * Note: the shm heap follows the shm table in memory.
			 * The next_free_offset field is initialized by the
			 * epiphany driver
			 */
			region->shm_seg.offset = sizeof(*tbl) + tbl->next_free_offset;

			region->shm_seg.addr = ((char*)tbl) + region->shm_seg.offset;
			region->shm_seg.paddr = ((char*)tbl->paddr_epi) + 
				region->shm_seg.offset;
			
			region->shm_seg.size = size;

			tbl->regions[i].valid = 1;

			tbl->free_space -= size;
			tbl->next_free_offset += size;

			diag(H_D2) {
				fprintf(stderr, "e_hal::shm_alloc_region(): allocated shm "
						"region: name %s, addr 0x%08lx, paddr 0x%08lx, "
						"offset 0x%08x, size 0x%08x\n", region->shm_seg.name,
						(unsigned long)region->shm_seg.addr,
						(unsigned long)region->shm_seg.paddr,
						(unsigned)region->shm_seg.offset,
						region->shm_seg.size);
			}
			
			break;
		}
	}

	return region;
}

static void
shm_compact_heap()
{
	/* Not yet implemented */
}