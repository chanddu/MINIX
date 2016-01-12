/* This file contains the "device dependent" part of a hard disk driver that
 * uses the ROM BIOS.  It makes a call and just waits for the transfer to
 * happen.  It is not interrupt driven and thus will (*) have poor performance.
 * The advantage is that it should work on virtually any PC, XT, 386, PS/2
 * or clone.  The demo disk uses this driver.  It is suggested that all
 * MINIX users try the other drivers, and use this one only as a last resort,
 * if all else fails.
 *
 * (*) The performance is within 10% of the AT driver for reads on any disk
 *     and writes on a 2:1 interleaved disk, it will be DMA_BUF_SIZE bytes
 *     per revolution for a minimum of 60 kb/s for writes to 1:1 disks.
 *
 * The file contains one entry point:
 *
 *	 bios_winchester_task:	main entry when system is brought up
 *
 *
 * Changes:
 *	30 Apr 1992 by Kees J. Bot: device dependent/independent split.
 *	14 May 2000 by Kees J. Bot: d-d/i rewrite.
 */

#include <minix/drivers.h>
#include <minix/blockdriver.h>
#include <minix/drvlib.h>
#include <minix/sysutil.h>
#include <minix/safecopies.h>
#include <sys/ioc_disk.h>
#include <machine/int86.h>
#include <assert.h>

/* Parameters for the disk drive. */
#define MAX_DRIVES         8	/* this driver supports 8 drives (d0 - d7)*/
#define NR_MINORS      (MAX_DRIVES * DEV_PER_DRIVE)
#define SUB_PER_DRIVE	(NR_PARTITIONS * NR_PARTITIONS)
#define NR_SUBDEVS	(MAX_DRIVES * SUB_PER_DRIVE)

/* Variables. */
PRIVATE struct wini {		/* main drive struct, one entry per drive */
  unsigned cylinders;		/* number of cylinders */
  unsigned heads;		/* number of heads */
  unsigned sectors;		/* number of sectors per track */
  unsigned open_ct;		/* in-use count */
  int drive_id;			/* Drive ID at BIOS level */
  int present;			/* Valid drive */
  int int13ext;			/* IBM/MS INT 13 extensions supported? */
  struct device part[DEV_PER_DRIVE];	/* disks and partitions */
  struct device subpart[SUB_PER_DRIVE]; /* subpartitions */
} wini[MAX_DRIVES], *w_wn;

PRIVATE int w_drive;			/* selected drive */
PRIVATE struct device *w_dv;		/* device's base and size */
PRIVATE char *bios_buf_v;
PRIVATE phys_bytes bios_buf_phys;
PRIVATE int remap_first = 0;		/* Remap drives for CD HD emulation */
#define BIOSBUF 16384

_PROTOTYPE(int main, (void) );
FORWARD _PROTOTYPE( struct device *w_prepare, (dev_t device) );
FORWARD _PROTOTYPE( struct device *w_part, (dev_t minor) );
FORWARD _PROTOTYPE( ssize_t w_transfer, (dev_t minor, int do_write,
	u64_t position, endpoint_t endpt, iovec_t *iov, unsigned int nr_req,
	int flags) );
FORWARD _PROTOTYPE( int w_do_open, (dev_t minor, int access) );
FORWARD _PROTOTYPE( int w_do_close, (dev_t minor) );
FORWARD _PROTOTYPE( void w_init, (void) );
FORWARD _PROTOTYPE( void w_geometry, (dev_t minor, struct partition *entry));
FORWARD _PROTOTYPE( int w_ioctl, (dev_t minor, unsigned int request,
	endpoint_t endpt, cp_grant_id_t grant) );

/* Entry points to this driver. */
PRIVATE struct blockdriver w_dtab = {
  BLOCKDRIVER_TYPE_DISK,	/* handle partition requests */
  w_do_open,	/* open or mount request, initialize device */
  w_do_close,	/* release device */
  w_transfer,	/* do the I/O */
  w_ioctl,	/* I/O control */
  NULL,		/* no cleanup needed */
  w_part,	/* return partition information structure */
  w_geometry,	/* tell the geometry of the disk */
  NULL,		/* leftover hardware interrupts */
  NULL,		/* ignore leftover alarms */
  NULL,		/* catch-all for unrecognized commands */
  NULL		/* no threading support */
};

/* SEF functions and variables. */
FORWARD _PROTOTYPE( void sef_local_startup, (void) );
FORWARD _PROTOTYPE( int sef_cb_init_fresh, (int type, sef_init_info_t *info) );

/*===========================================================================*
 *				bios_winchester_task			     *
 *===========================================================================*/
PUBLIC int main(void)
{
  /* SEF local startup. */
  sef_local_startup();

  /* Call the generic receive loop. */
  blockdriver_task(&w_dtab);

  return(OK);
}

/*===========================================================================*
 *			       sef_local_startup			     *
 *===========================================================================*/
PRIVATE void sef_local_startup(void)
{
  /* Register init callbacks. */
  sef_setcb_init_fresh(sef_cb_init_fresh);
  sef_setcb_init_lu(sef_cb_init_fresh);

  /* Register live update callbacks. */
  sef_setcb_lu_prepare(sef_cb_lu_prepare_always_ready);
  sef_setcb_lu_state_isvalid(sef_cb_lu_state_isvalid_standard);

  /* Let SEF perform startup. */
  sef_startup();
}

/*===========================================================================*
 *		            sef_cb_init_fresh                                *
 *===========================================================================*/
PRIVATE int sef_cb_init_fresh(int type, sef_init_info_t *UNUSED(info))
{
/* Initialize the bios_wini driver. */
  long v;

  v = 0;
  env_parse("bios_remap_first", "d", 0, &v, 0, 1);
  remap_first = v;

  /* Announce we are up! */
  blockdriver_announce(type);

  return(OK);
}

/*===========================================================================*
 *				w_prepare				     *
 *===========================================================================*/
PRIVATE struct device *w_prepare(dev_t device)
{
/* Prepare for I/O on a device. */

  if (device < NR_MINORS) {			/* d0, d0p[0-3], d1, ... */
	w_drive = device / DEV_PER_DRIVE;	/* save drive number */
	w_wn = &wini[w_drive];
	w_dv = &w_wn->part[device % DEV_PER_DRIVE];
  } else
  if ((unsigned) (device -= MINOR_d0p0s0) < NR_SUBDEVS) {/*d[0-7]p[0-3]s[0-3]*/
	w_drive = device / SUB_PER_DRIVE;
	w_wn = &wini[w_drive];
	w_dv = &w_wn->subpart[device % SUB_PER_DRIVE];
  } else {
	return(NULL);
  }
  if (w_drive >= MAX_DRIVES || !w_wn->present)
  	return NULL;
  return(w_dv);
}

/*===========================================================================*
 *				w_part					     *
 *===========================================================================*/
PRIVATE struct device *w_part(dev_t minor)
{
/* Return a pointer to the partition information of the given minor device. */

  return w_prepare(minor);
}

/*===========================================================================*
 *				w_transfer				     *
 *===========================================================================*/
PRIVATE ssize_t w_transfer(
  dev_t minor,			/* minor device number */
  int do_write,			/* read or write? */
  u64_t pos64,			/* offset on device to read or write */
  endpoint_t proc_nr,		/* process doing the request */
  iovec_t *iov,			/* pointer to read or write request vector */
  unsigned int nr_req,		/* length of request vector */
  int UNUSED(flags)		/* transfer flags */
)
{
  struct wini *wn;
  iovec_t *iop, *iov_end = iov + nr_req;
  int r, errors;
  unsigned count;
  vir_bytes chunk, nbytes;
  unsigned long block;
  vir_bytes i13e_rw_off, rem_buf_size;
  unsigned secspcyl;
  struct int13ext_rw {
	u8_t	len;
	u8_t	res1;
	u16_t	count;
	u16_t	addr[2];
	u32_t	block[2];
  } *i13e_rw;
  struct reg86u reg86;
  u32_t lopos;
  ssize_t total;

  if (w_prepare(minor) == NULL) return(ENXIO);

  wn = w_wn;
  secspcyl = wn->heads * wn->sectors;

  lopos= ex64lo(pos64);

  /* Check disk address. */
  if ((lopos & SECTOR_MASK) != 0) return(EINVAL);

  total = 0;
  errors = 0;

  i13e_rw_off= BIOSBUF-sizeof(*i13e_rw);
  rem_buf_size= (i13e_rw_off & ~SECTOR_MASK);
  i13e_rw = (struct int13ext_rw *) (bios_buf_v + i13e_rw_off);
  assert(rem_buf_size != 0);

  while (nr_req > 0) {
	/* How many bytes to transfer? */
	nbytes = 0;
	for (iop = iov; iop < iov_end; iop++) {
		if (nbytes + iop->iov_size > rem_buf_size) {
			/* Don't do half a segment if you can avoid it. */
			if (nbytes == 0) nbytes = rem_buf_size;
			break;
		}
		nbytes += iop->iov_size;
	}
	if ((nbytes & SECTOR_MASK) != 0) return(EINVAL);

	/* Which block on disk and how close to EOF? */
	if (cmp64(pos64, w_dv->dv_size) >= 0) return(total);	/* At EOF */
	if (cmp64(add64u(pos64, nbytes), w_dv->dv_size) > 0) {
		u64_t n;
		n = sub64(w_dv->dv_size, pos64);
		assert(ex64hi(n) == 0);
		nbytes = ex64lo(n);
	}
	block = div64u(add64(w_dv->dv_base, pos64), SECTOR_SIZE);

	/* Degrade to per-sector mode if there were errors. */
	if (errors > 0) nbytes = SECTOR_SIZE;

	if (do_write) {
		/* Copy from user space to the DMA buffer. */
		count = 0;
		for (iop = iov; count < nbytes; iop++) {
			chunk = iop->iov_size;
			if (count + chunk > nbytes) chunk = nbytes - count;
			assert(chunk <= rem_buf_size);

			if(proc_nr != SELF) {
			   	r=sys_safecopyfrom(proc_nr,
					(cp_grant_id_t) iop->iov_addr,
		       			0, (vir_bytes) (bios_buf_v+count),
					chunk, D);
				if (r != OK)
					panic("copy failed: %d", r);
			} else {
				memcpy(bios_buf_v+count,
					(char *) iop->iov_addr, chunk);
			}
			count += chunk;
		}
	}

	/* Do the transfer */
	if (wn->int13ext) {
		i13e_rw->len = 0x10;
		i13e_rw->res1 = 0;
		i13e_rw->count = nbytes >> SECTOR_SHIFT;
		i13e_rw->addr[0] = bios_buf_phys % HCLICK_SIZE;
		i13e_rw->addr[1] = bios_buf_phys / HCLICK_SIZE;
		i13e_rw->block[0] = block;
		i13e_rw->block[1] = 0;

		/* Set up an extended read or write BIOS call. */
		reg86.u.b.intno = 0x13;
		reg86.u.w.ax = do_write ? 0x4300 : 0x4200;
		reg86.u.b.dl = wn->drive_id;
		reg86.u.w.si = (bios_buf_phys + i13e_rw_off) % HCLICK_SIZE;
		reg86.u.w.ds = (bios_buf_phys + i13e_rw_off) / HCLICK_SIZE;
	} else {
		/* Set up an ordinary read or write BIOS call. */
		unsigned cylinder = block / secspcyl;
		unsigned sector = (block % wn->sectors) + 1;
		unsigned head = (block % secspcyl) / wn->sectors;

		reg86.u.b.intno = 0x13;
		reg86.u.b.ah = do_write ? 0x03 : 0x02;
		reg86.u.b.al = nbytes >> SECTOR_SHIFT;
		reg86.u.w.bx = bios_buf_phys % HCLICK_SIZE;
		reg86.u.w.es = bios_buf_phys / HCLICK_SIZE;
		reg86.u.b.ch = cylinder & 0xFF;
		reg86.u.b.cl = sector | ((cylinder & 0x300) >> 2);
		reg86.u.b.dh = head;
		reg86.u.b.dl = wn->drive_id;
	}

	r= sys_int86(&reg86);
	if (r != OK)
		panic("BIOS call failed: %d", r);

	if (reg86.u.w.f & 0x0001) {
		/* An error occurred, try again sector by sector unless */
		if (++errors == 2) return(EIO);
		continue;
	}

	if (!do_write) {
		/* Copy from the DMA buffer to user space. */
		count = 0;
		for (iop = iov; count < nbytes; iop++) {
			chunk = iop->iov_size;
			if (count + chunk > nbytes) chunk = nbytes - count;
			assert(chunk <= rem_buf_size);

			if(proc_nr != SELF) {
			   	r=sys_safecopyto(proc_nr, iop->iov_addr, 
				       	0, (vir_bytes) (bios_buf_v+count),
				       	chunk, D);

				if (r != OK)
					panic("sys_safecopy failed: %d", r);
			} else {
				memcpy((char *) iop->iov_addr,
					bios_buf_v+count, chunk);
			}
			count += chunk;
		}
	}

	/* Book the bytes successfully transferred. */
	pos64 = add64ul(pos64, nbytes);
	total += nbytes;
	while (nbytes > 0) {
		if (nbytes < iov->iov_size) {
			/* Not done with this one yet. */
			iov->iov_size -= nbytes;
			break;
		}
		nbytes -= iov->iov_size;
		iov->iov_size = 0;
		iov++;
		nr_req--;
	}
  }
  return(total);
}

/*============================================================================*
 *				w_do_open				      *
 *============================================================================*/
PRIVATE int w_do_open(dev_t minor, int UNUSED(access))
{
/* Device open: Initialize the controller and read the partition table. */

  static int init_done = FALSE;

  if (!init_done) { w_init(); init_done = TRUE; }

  if (w_prepare(minor) == NULL) return(ENXIO);

  if (w_wn->open_ct++ == 0) {
	/* Partition the disk. */
	partition(&w_dtab, w_drive * DEV_PER_DRIVE, P_PRIMARY, 0);
  }
  return(OK);
}

/*============================================================================*
 *				w_do_close				      *
 *============================================================================*/
PRIVATE int w_do_close(dev_t minor)
{
/* Device close: Release a device. */

  if (w_prepare(minor) == NULL) return(ENXIO);
  w_wn->open_ct--;
  return(OK);
}

/*===========================================================================*
 *				w_init					     *
 *===========================================================================*/
PRIVATE void w_init(void)
{
/* This routine is called at startup to initialize the drive parameters. */

  int r, drive, drive_id, nr_drives;
  struct wini *wn;
  unsigned long capacity;
  struct int13ext_params {
	u16_t	len;
	u16_t	flags;
	u32_t	cylinders;
	u32_t	heads;
	u32_t	sectors;
	u32_t	capacity[2];
	u16_t	bts_per_sec;
	u16_t	config[2];
  } *i13e_par;
  struct reg86u reg86;

  /* Ask the system task for a suitable buffer */
  if(!(bios_buf_v = alloc_contig(BIOSBUF, AC_LOWER1M, &bios_buf_phys))) {
  	panic("allocating bios buffer failed: %d", ENOMEM);
  }

  if (bios_buf_phys+BIOSBUF > 0x100000)
  	panic("bad BIOS buffer / phys: %d", bios_buf_phys);
#if 0
  printf("bios_wini: got buffer size %d, virtual 0x%x, phys 0x%x\n",
  		BIOSBUF, bios_buf_v, bios_buf_phys);
#endif

  i13e_par = (struct int13ext_params *) bios_buf_v;

  /* Get the geometry of the drives */
  for (drive = 0; drive < MAX_DRIVES; drive++) {
  	if (remap_first)
  	{
  		if (drive == 7)
			drive_id= 0x80;
		else
			drive_id= 0x80 + drive + 1;
  	}
  	else
		drive_id= 0x80 + drive;

	(void) w_prepare(drive * DEV_PER_DRIVE);
	wn = w_wn;
	wn->drive_id= drive_id;

	reg86.u.b.intno = 0x13;
	reg86.u.b.ah = 0x08;	/* Get drive parameters. */
	reg86.u.b.dl = drive_id;
	r= sys_int86(&reg86);
	if (r != OK)
		panic("BIOS call failed: %d", r);

	nr_drives = !(reg86.u.w.f & 0x0001) ? reg86.u.b.dl : drive;
	if (drive_id >= 0x80 + nr_drives) continue;
	wn->present= 1;

	wn->heads = reg86.u.b.dh + 1;
	wn->sectors = reg86.u.b.cl & 0x3F;
	wn->cylinders = (reg86.u.b.ch | ((reg86.u.b.cl & 0xC0) << 2)) + 1;

	capacity = (unsigned long) wn->cylinders * wn->heads * wn->sectors;

	reg86.u.b.intno = 0x13;
	reg86.u.b.ah = 0x41;	/* INT 13 Extensions - Installation check */
	reg86.u.w.bx = 0x55AA;
	reg86.u.b.dl = drive_id;

	r= sys_int86(&reg86);
	if (r != OK)
	    panic("BIOS call failed: %d", r);

	if (!(reg86.u.w.f & 0x0001) && reg86.u.w.bx == 0xAA55
				&& (reg86.u.w.cx & 0x0001)) {
		/* INT 13 Extensions available. */
		i13e_par->len = 0x001E;	/* Input size of parameter packet */
		reg86.u.b.intno = 0x13;
		reg86.u.b.ah = 0x48;	/* Ext. Get drive parameters. */
		reg86.u.b.dl = drive_id;
		reg86.u.w.si = bios_buf_phys % HCLICK_SIZE;
		reg86.u.w.ds = bios_buf_phys / HCLICK_SIZE;

		r= sys_int86(&reg86);
		if (r != OK)
			panic("BIOS call failed: %d", r);

		if (!(reg86.u.w.f & 0x0001)) {
			wn->int13ext = 1;	/* Extensions can be used. */
			capacity = i13e_par->capacity[0];
			if (i13e_par->capacity[1] != 0) capacity = 0xFFFFFFFF;
		}
	}

	if (wn->int13ext) {
		printf("bios-d%u: %lu sectors\n", w_drive, capacity);
	} else {
		printf("bios-d%u: %d cylinders, %d heads, "
			"%d sectors per track\n",
			w_drive, wn->cylinders, wn->heads, wn->sectors);
	}
	wn->part[0].dv_size = mul64u(capacity, SECTOR_SIZE);
  }
}

/*============================================================================*
 *				w_geometry				      *
 *============================================================================*/
PRIVATE void w_geometry(dev_t minor, struct partition *entry)
{
  if (w_prepare(minor) == NULL) return;

  entry->cylinders = w_wn->cylinders;
  entry->heads = w_wn->heads;
  entry->sectors = w_wn->sectors;
}

/*============================================================================*
 *				w_ioctl					      *
 *============================================================================*/
PRIVATE int w_ioctl(dev_t minor, unsigned int request, endpoint_t endpt,
	cp_grant_id_t grant)
{
	int count;

	if (w_prepare(minor) == NULL) return ENXIO;

	if (request == DIOCOPENCT) {
		count = w_wn->open_ct;
		return sys_safecopyto(endpt, grant, 0, (vir_bytes)&count,
			sizeof(count), D);
	}

	return EINVAL;
}
