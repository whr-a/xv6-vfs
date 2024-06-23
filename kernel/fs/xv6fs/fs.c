// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "fs/vfs.h"
#include "fs/vfs_defs.h"
#include "types.h"
#include "riscv.h"
#include "kernel/defs.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"
#include "xv6_fcntl.h"
#include <time.h>

#define min(a, b) ((a) < (b) ? (a) : (b))
// there should be one superblock per disk device, but we run with
// only one device
struct xv6fs_super_block sb;
void readblock(struct inode *ip);
void xv6fs_fileclose(struct file *f);

struct filesystem_operations xv6fs_op = {
    // .mount = xv6_mount,
    // .umount = xv6_umount,
    .alloc_inode = xv6fs_ialloc,
    .write_inode = xv6fs_iupdate,
    // .release_inode = xv6_release_inode,
    // .free_inode = xv6_free_inode,
    .trunc = xv6fs_itrunc,
    .open = xv6fs_open,
    .close = xv6fs_fileclose,
    .read = xv6fs_readi,
    .write = xv6fs_writei,
    .create = xv6fs_create,
    .link = xv6fs_link,
    .unlink = xv6fs_unlink,
    .dirlookup = xv6fs_dirlookup,
    // .release_dentry = xv6_release_dentry,
    .isdirempty = xv6fs_isdirempty,
    .init = xv6fs_fsinit,
    .read_block = readblock,
};
struct filesystem_type xv6fs_type = {
    .type = "xv6fs",
    .op = &xv6fs_op,
};
// Read the super block.
static void
readsb(int dev, struct xv6fs_super_block *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}
void
readblock(struct inode *ip){
  // printf("in readblock\n");
  struct buf *bp;
  struct dinode *dip;
  if(ip->private == 0){
    struct xv6fs_inode *ipp = kalloc();
    bp = bread(ip->dev, IBLOCK(ip->inum, sb));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    // printf("**%d**\n",dip->type);
    ip->type = dip->type;
    ipp->dev = 1;
    ipp->major = dip->major;
    ipp->minor = dip->minor;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ipp->addrs, dip->addrs, sizeof(ipp->addrs));
    brelse(bp);
    ipp->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
    ip->private = ipp;
  }
  // printf("---r%d---\n", ip->inum);
  // printf("out readblock\n");
}
// Init fs
void
xv6fs_fsinit() {
  // printf("in fsinit\n");
  readsb(1, &sb);
  if(sb.magic != FSMAGIC)
    panic("invalid file system");
  // printf("out fsinit\n");
}

// Zero a block.
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  bwrite(bp);
  brelse(bp);
}

// Blocks.

// Allocate a zeroed disk block.
// returns 0 if out of disk space.
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb.size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb));
    for(bi = 0; bi < BPB && b + bi < sb.size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // Is block free?
        bp->data[bi/8] |= m;  // Mark block in use.
        bwrite(bp);
        brelse(bp);
        bzero(dev, b + bi);
        // printf("balloc:%d\n",b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  printf("balloc: out of blocks\n");
  return 0;
}

// Free a disk block.
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;
  // printf("bfree:dev:%d, uint:%d\n", dev, b);
  bp = bread(dev, BBLOCK(b, sb));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  bwrite(bp);
  brelse(bp);
}

struct inode*
xv6fs_ialloc(struct super_block *vfs_sb, short type)
{
  // printf("in ialloc\n");
  int inum;
  struct buf *bp;
  struct dinode *dip;
  // struct xv6fs_super_block* xv6_sb = sb->private;
  for(inum = 1; inum < sb.ninodes; inum++){
    bp = bread(vfs_sb->root->dev, IBLOCK(inum, sb));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // a free inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      bwrite(bp);   // mark it allocated on the disk
      brelse(bp);
      struct inode* tmp = iget(vfs_sb->root->dev, inum);
      tmp->op = &xv6fs_op;
      tmp->sb = root;
      tmp->dev = 1;
      tmp->private = NULL;
      // printf("out ialloc\n");
      return tmp;
    }
    brelse(bp);
  }
  printf("ialloc: no inodes\n");
  return 0;
}

// Copy a modified in-memory inode to disk.
// Must be called after every change to an ip->xxx field
// that lives on disk.
// Caller must hold ip->lock.
void
xv6fs_iupdate(struct inode *ip)
{
  // printf("in iupdate\n");
  struct buf *bp;
  struct dinode *dip;
  struct xv6fs_inode* ipp = ip->private;
  bp = bread(ip->dev, IBLOCK(ip->inum, sb));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ipp->major;
  dip->minor = ipp->minor;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ipp->addrs, sizeof(ipp->addrs));
  bwrite(bp);
  brelse(bp);
  // printf("out iupdate\n");
}

static uint
bmap(struct xv6fs_inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      // printf("I get1 %d \n", addr);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // Load indirect block, allocating if necessary.
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      // printf("I get2 %d \n", addr);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      // printf("I get3 %d \n", addr);
      if(addr){
        a[bn] = addr;
        bwrite(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// Truncate inode (discard contents).
// Caller must hold ip->lock.
void
xv6fs_itrunc(struct inode *ip)
{
  // printf("in itrunc\n");
  int i, j;
  struct buf *bp;
  uint *a;
  struct xv6fs_inode* ipp=ip->private;
  // printf("ip->dev:%d\n",ip->dev);
  for(i = 0; i < NDIRECT; i++){
    if(ipp->addrs[i]){
      bfree(ip->dev, ipp->addrs[i]);
      ipp->addrs[i] = 0;
    }
  }

  if(ipp->addrs[NDIRECT]){
    bp = bread(ip->dev, ipp->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ipp->addrs[NDIRECT]);
    ipp->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  xv6fs_iupdate(ip);
  // printf("out itrunc\n");
}


// Read data from inode.
// Caller must hold ip->lock.
// If user_dst==1, then dst is a user virtual address;
// otherwise, dst is a kernel address.
int
xv6fs_readi(struct inode *ip, char user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;
  struct xv6fs_inode* ipp=ip->private;
  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint addr = bmap(ipp, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// Write data to inode.
// Caller must hold ip->lock.
// If user_src==1, then src is a user virtual address;
// otherwise, src is a kernel address.
// Returns the number of bytes successfully written.
// If the return value is less than the requested n,
// there was an error of some kind.
int
xv6fs_writei(struct inode *ip, char user_src, uint64 src, uint off, uint n)
{
  // printf("in writei\n");
  uint tot, m;
  struct buf *bp;
  struct xv6fs_inode* ipp=ip->private;
  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint addr = bmap(ipp, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    bwrite(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  xv6fs_iupdate(ip);
  // printf("out writei\n");
  return tot;
}

// Directories

int
xv6fs_namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// Look for a directory entry in a directory.
// If found, set *poff to byte offset of entry.
struct dentry*
xv6fs_dirlookup(struct inode *dp, const char *name)
{
  // printf("in dirlookup\n");
  // printf("name: %s\n",name);
  uint off, inum;
  struct xv6fs_dentry de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(xv6fs_readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    // printf("dename: %s\n",de.name);
    if(de.inum == 0)
      continue;
    if(xv6fs_namecmp(name, de.name) == 0){
      // entry matches path element
      struct dentry* ret = kalloc();
      ret->private = kalloc();
      *(uint*)(ret->private) = off;
      ret->op = &xv6fs_op;
      inum = de.inum;
      ret->inode = iget(dp->dev, inum);
      // printf("out dirlookup\n");
      return ret;
    }
  }
  struct dentry* ret = kalloc();
  ret->private = kalloc();
  ret->inode = 0;
  // printf("out dirlookup\n");
  return ret;
}

int
xv6fs_link(struct dentry *target)
{
  // printf("in link\n");
  struct inode *dp = target->inode;
  char name[DIRSIZ];
  strncpy(name, target->name, DIRSIZ);
  uint inum = *(uint*)(target->private);
  int off;
  struct xv6fs_dentry de;
  struct inode *ip;
  struct dentry* dir_ret = xv6fs_dirlookup(dp, name);
  ip = dir_ret -> inode;
  // Check that name is not present.
  if(ip != 0){
    iput(ip);
    kfree(dir_ret->private);
    kfree(dir_ret);
    // printf("out link\n");
    return -1;
  }

  // Look for an empty dentry.
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(xv6fs_readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)){
      kfree(dir_ret->private);
      kfree(dir_ret);
      panic("dirlink read");
    }
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(xv6fs_writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de)){
    kfree(dir_ret->private);
    kfree(dir_ret);
    // printf("out link\n");
    return -1;
  }
  
  kfree(dir_ret->private);
  kfree(dir_ret);
  // printf("out link\n");
  return 0;
}

int
xv6fs_unlink(struct dentry *d){
  // printf("in unlink\n");
  struct inode *ip, *dp;
  struct xv6fs_dentry de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  strncpy(path, (char*)(d->private), MAXPATH);

  if((dp = nameiparent(path, name)) == 0){
    // printf("out unlink\n");
    return -1;
  }

  ilock(dp);

  // Cannot unlink "." or "..".
  if(xv6fs_namecmp(name, ".") == 0 || xv6fs_namecmp(name, "..") == 0){
    iunlockput(dp);
    // printf("out unlink\n");
    return -1;
  }
  struct dentry* dir_ret = xv6fs_dirlookup(dp, name);
  ip = dir_ret->inode;
  off = *(uint*)(dir_ret->private);
  if(ip == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !ip->op->isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(xv6fs_writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    xv6fs_iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  xv6fs_iupdate(ip);
  iunlockput(ip);
  kfree(dir_ret->private);
  kfree(dir_ret);
  // printf("out unlink\n");
  return 0;

bad:
  kfree(dir_ret->private);
  kfree(dir_ret);
  iunlockput(dp);
  // printf("out unlink\n");
  return -1;
}
int
xv6fs_isdirempty (struct inode *dp){
  // printf("in isdirempty\n");
  int off;
  struct xv6fs_dentry de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(xv6fs_readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0){
      // printf("out isdirempty\n");
      return 0;
    }
      
  }
  // printf("out isdirempty\n");
  return 1;
}
int
xv6fs_create(struct inode *dp, struct dentry *target, short type, short major, short minor)
{
  // printf("in create\n");

  struct inode *ip;
  char name[DIRSIZ];
  strncpy(name, (char*)(target->private), DIRSIZ);
  ilock(dp);
  struct dentry* dir_ret = xv6fs_dirlookup(dp, name);
  ip = dir_ret->inode;
  if(ip != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE && (ip->type == T_FILE || ip->type == T_DEVICE)){
      target->inode = ip;
      kfree(dir_ret->private);
      kfree(dir_ret);
      // printf("out create1\n");
      return 1;
    }
    iunlockput(ip);
    kfree(dir_ret->private);
    kfree(dir_ret);
    // printf("out create2\n");
    return 0;
  }

  if((ip = xv6fs_ialloc(root, type)) == 0){
    iunlockput(dp);
    kfree(dir_ret->private);
    kfree(dir_ret);
    // printf("out create3\n");
    return 0;
  }

  // printf("fuck1\n");
  ilock(ip);
  struct xv6fs_inode* ipp = ip->private;
  ipp->major = major;
  ipp->minor = minor;
  ip->nlink = 1;
  xv6fs_iupdate(ip);

  if(type == T_DIR){  // Create . and .. entries.
    // No ip->nlink++ for ".": avoid cyclic ref count.
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // now that success is guaranteed:
    dp->nlink++;  // for ".."
    xv6fs_iupdate(dp);
  }

  iunlockput(dp);
  target->inode = ip;
  kfree(dir_ret->private);
  kfree(dir_ret);
  // printf("out create4\n");
  return 1;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  xv6fs_iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  kfree(dir_ret->private);
  kfree(dir_ret);
  // printf("out create5\n");
  return 0;
}

struct file* 
xv6fs_open (struct inode *ip, uint omode){
  // printf("in open\n");
  struct xv6fs_inode* ipp = ip->private;
  struct file *f;
  if(ip->type == T_DEVICE && (ipp->major < 0 || ipp->major >= NDEV)){
    iunlockput(ip);
    // printf("out open\n");
    return 0;
  }

  if((f = filealloc()) == 0){
    if(f)
      xv6fs_fileclose(f);
    iunlockput(ip);
    // printf("out open\n");
    return 0;
  }

  if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    // f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->inode = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  // printf("out open\n");
  return f;
}
void
xv6fs_fileclose(struct file *f)
{
  // printf("in fileclose\n");
  struct file ff;

  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    // printf("out fileclose\n");
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;

  // if(ff.type == FD_PIPE){
  //   pipeclose(ff.pipe, ff.writable);
  // } else 
  if(ff.type == FD_INODE || ff.type == FD_DEVICE){
    iput(ff.inode);
  }
  // printf("out fileclose\n");
}