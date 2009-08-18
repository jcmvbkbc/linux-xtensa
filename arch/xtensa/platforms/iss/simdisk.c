/*
 * arch/xtensa/platform-iss/simdisk.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2005 Tensilica Inc.
 *   Authors	Victor Prupis
 */

#include	<linux/module.h>
#include	<linux/moduleparam.h>
#include	<linux/kernel.h>
#include	<linux/init.h>
#include	<linux/string.h>
#include	<linux/blkdev.h>
#include	<linux/bio.h>
#include	<linux/proc_fs.h>
#if 0
#include	<asm/semaphore.h>
#endif
#include	<asm/uaccess.h>
#include	<platform/simcall.h>

#define SIMDISK_MAJOR 240

#if defined (CONFIG_BLK_DEV_SIMDISK_COUNT) && (CONFIG_BLK_DEV_SIMDISK_COUNT > 0)

#define FILENAMELEN 1024
#define CONFIG_BLK_DEV_BLOCKSIZE 512
#define SIMDISK_MINORS 1

#ifdef MODULE
#define EXTERN
#else
#define EXTERN extern
#endif

struct simdisk {
  char filename[FILENAMELEN + 1];
  spinlock_t lock;
  struct request_queue * queue;
  struct gendisk * gd;
  struct proc_dir_entry * procfile;
  int users;
  int size;
  int fd;
};

////////////////////////////////////////////////////////////////

EXTERN int errno;
static char * sd_filename[CONFIG_BLK_DEV_SIMDISK_COUNT] = {
  CONFIG_SIMDISK0_FILENAME,
#ifdef CONFIG_SIMDISK1_FILENAME
  CONFIG_SIMDISK1_FILENAME,
#endif
};

static int hardsect_size = CONFIG_BLK_DEV_BLOCKSIZE;
static int simdisk_major = SIMDISK_MAJOR;

static int simcall (int a, int b, int c, int d, int e, int f) __attribute__((__noinline__));
static int simcall (int a, int b, int c, int d, int e, int f)
{
  int ret;
  __asm__ __volatile__ ("simcall\n"
                         "mov %0, a2\n"
			 "mov %1, a3\n"
			 : "=a" (ret), "=a" (errno)
			 : : "a2", "a3");
  return ret;
}

static void simdisk_transfer(struct simdisk * dev, unsigned long sector,
                            unsigned long nsect, char * buffer, int write)
{
  int err;
  unsigned long offset = sector * hardsect_size;
  unsigned long nbytes = nsect * hardsect_size;

  if ((offset + nbytes) > dev->size) {
    printk (KERN_NOTICE "Beyond-end %s (%ld %ld)\n", write? "write": "read", offset, nbytes);
    return;
  }

  if ((unsigned long) buffer < 0xd0000000)
    printk (KERN_NOTICE "%s %p: %ld %ld\n", write? "W": "R", buffer, offset, nbytes);
  spin_lock(&dev->lock);
  simcall(SYS_lseek, dev->fd, offset, SEEK_SET,    0,0);
  if (write) {
    err = simcall(SYS_write, dev->fd, (unsigned long) buffer, nbytes,    0,0);
  } else {
    err = simcall(SYS_read, dev->fd, (unsigned long) buffer, nbytes,    0,0);
  }
  spin_unlock(&dev->lock);

  if (err < 0) {
    printk (KERN_NOTICE "SIMDISK: IO error %d\n", -err);
  }
}

static int simdisk_xfer_bio(struct simdisk * dev, struct bio * bio)
{
  int i;
  struct bio_vec * bvec;
  sector_t sector = bio->bi_sector;

  bio_for_each_segment(bvec, bio, i) {
    char * buffer = __bio_kmap_atomic(bio, i, KM_USER0);
    simdisk_transfer(dev, sector, bio_cur_sectors(bio), buffer, bio_data_dir(bio) == WRITE);
    sector += bio_cur_sectors(bio);
    __bio_kunmap_atomic(bio, KM_USER0);
  }
  return 0;
}

static int simdisk_make_request(struct request_queue * q, struct bio * bio)
{
  struct simdisk * dev = q->queuedata;
  int rw = bio_rw(bio);
  int status;

  if (rw == READA) rw = READ;

  status = simdisk_xfer_bio(dev, bio);
  // Rubini uses bio_endio(bio, bio->bi_size, status);, yet <linux/bio.h>
  // defines bio_endio(bio, error)
  bio_endio(bio, status);
  return 0;
}

////////////////////////////////////////////////////////////////

static int simdisk_open(struct inode * inode, struct file * filp)
{
  struct simdisk * dev = inode->i_bdev->bd_disk->private_data;
  filp->private_data = dev;
  spin_lock(&dev->lock);
  if (!dev->users)
    check_disk_change(inode->i_bdev);
  dev->users++;
  spin_unlock(&dev->lock);
  return 0;
}

static int simdisk_release(struct inode * inode, struct file * filp)
{
  struct simdisk * dev = inode->i_bdev->bd_disk->private_data;
  spin_lock(&dev->lock);
  dev->users--;
  spin_unlock(&dev->lock);
  return 0;
}

static int simdisk_ioctl(struct inode * inode, struct file * filp,
                         unsigned int cmd, unsigned long parm)
{
  return -ENOSYS;
}

static struct block_device_operations simdisk_ops = {
  .owner		= THIS_MODULE,
  .open			= simdisk_open,
  .release		= simdisk_release,
  .ioctl		= simdisk_ioctl,
};

static struct simdisk * sddev = 0;

static int simdisk_attach(struct simdisk * dev, char * filename)
{
  if (dev->fd != -1)
    return -EBUSY;

  snprintf(dev->filename, FILENAMELEN, "%s", filename);
  if ((dev->fd = simcall(SYS_open, (unsigned long) dev->filename, O_RDWR, 0,0,0)) == -1) {
    printk ("Can't open %s: %d\n", dev->filename, errno);
    return -ENODEV;
  }

  dev->size = simcall(SYS_lseek, dev->fd, 0, SEEK_END,   0, 0);
  simcall(SYS_lseek, dev->fd, 0, SEEK_SET,   0, 0);
  set_capacity(dev->gd, dev->size / 512);
  printk ("SIMDISK: %s=%s\n", dev->gd->disk_name, dev->filename);

  return 0;
}

static int simdisk_detach(struct simdisk * dev)
{
  if (dev->fd == -1)
    return 0;
  if (dev->users != 0)
    return -EBUSY;
  simcall(SYS_close, dev->fd,     0, 0, 0, 0);
  dev->fd = -1;
  return 0;
}

static int proc_read_simdisk(char * page, char ** start, off_t off,
                             int count, int * eof, void * data)
{
  int len;
  struct simdisk * dev = (struct simdisk *) data;
  len = sprintf(page, "%s\n", dev->filename);
  return len;
}

static int proc_write_simdisk (struct file * file, const char * buffer,
                               unsigned long count, void * data)
{
  char tmp[FILENAMELEN + 1];
  struct simdisk * dev = (struct simdisk *) data;
  int err;

  if ((err = simdisk_detach(dev)) != 0)
    return err;
  if (count > FILENAMELEN)
    return -ENAMETOOLONG;
  if (copy_from_user(tmp, buffer, count))
    return -EFAULT;
  if (tmp[count - 1] == '\n')
    tmp[count - 1] = 0;
  simdisk_attach(dev, tmp);
  return count;
}

static int __init simdisk_setup(struct simdisk * dev, int which,
                                struct proc_dir_entry * procdir)
{
  char tmp[2];
  dev->fd = -1;
  dev->filename[0] = 0;
  spin_lock_init(&dev->lock);

  if ((dev->queue = blk_alloc_queue(GFP_KERNEL)) == 0) {
    printk ("blk_alloc_queue failed\n");
    goto out_alloc_queue;
  }

  blk_queue_make_request(dev->queue, simdisk_make_request);
  blk_queue_hardsect_size(dev->queue, hardsect_size);
  dev->queue->queuedata = dev;

  if ((dev->gd = alloc_disk(SIMDISK_MINORS)) == 0) {
    printk ("alloc_disk failed\n");
    goto out_alloc_disk;
  }
  dev->gd->major = simdisk_major;
  dev->gd->first_minor = which;
  dev->gd->fops = &simdisk_ops;
  dev->gd->queue = dev->queue;
  dev->gd->private_data = dev;
  snprintf(dev->gd->disk_name, 32, "simdisk%d", which);
  set_capacity(dev->gd, 0);
  add_disk(dev->gd);

  tmp[0] = '0' + which; tmp[1] = 0;
  dev->procfile = create_proc_entry(tmp, 0644, procdir);
  dev->procfile->data = dev;
  dev->procfile->read_proc = proc_read_simdisk;
  dev->procfile->write_proc = proc_write_simdisk;
  return 0;

out_alloc_disk:
  blk_cleanup_queue(dev->queue);
out_alloc_queue:
  simcall(SYS_close, dev->fd,   0,0,0,0);
  return -EIO;
}

static int __init simdisk_init(void)
{
  int i;
  struct proc_dir_entry * simdisk_procdir;

  if (register_blkdev(simdisk_major, "simdisk") < 0) {
    printk("SIMDISK: register_blkdev: %d\n", simdisk_major);
    return -EIO;
  }
  printk("SIMDISK: major: %d\n", simdisk_major);

  if ((sddev = kmalloc(CONFIG_BLK_DEV_SIMDISK_COUNT * sizeof (struct simdisk), GFP_KERNEL)) == 0)
    goto out_unregister;

  if ((simdisk_procdir = proc_mkdir("simdisk", 0)) == 0)
    goto out_unregister;
  simdisk_procdir->owner = THIS_MODULE;

  for (i = 0; i < CONFIG_BLK_DEV_SIMDISK_COUNT; i++) {
    if (simdisk_setup(&sddev[i], i, simdisk_procdir) == 0) {
      if (sd_filename[i] != 0)
        simdisk_attach(&sddev[i], sd_filename[i]);
    }
  }

  return 0;
out_unregister:
  unregister_blkdev(simdisk_major, "simdisk");
  return -ENOMEM;
}

static void simdisk_exit(void)
{
}

module_init(simdisk_init);
module_exit(simdisk_exit);

#ifndef MODULE
static int __init simdisk_filename(char * str)
{
  return 0;
}
__setup("simdisk_filename=", simdisk_filename);
#endif

// module_param(sd_filename, charp, 0);
MODULE_PARM_DESC(sd_filename, "Backing storage filename.");

// MODULE_ALIAS_BLOCKDEV_MAJOR(SIMDISK_MAJOR);

MODULE_LICENSE("GPL");
#endif // CONFIG_BLK_DEV_SIMDISK_COUNT
