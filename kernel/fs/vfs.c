#include "types.h"
#include "riscv.h"
#include "kernel/defs.h"
#include "defs.h"
#include "param.h"
#include "buf.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "stat.h"
#include "proc.h"
#include "vfs.h"
#include "vfs_defs.h"
#include "xv6fs/fs.h"
#include <time.h>
struct super_block *root;

struct it{
  struct inode inode[NINODE];
} itable;

struct devsw devsw[NDEV];
struct ft{
  struct file file[NFILE];
} ftable;

void
iinit()
{
  int i = 0;
  
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}
struct inode* iget(uint dev, uint inum);
void vfs_init()
{
  root = kalloc();
  root->type = &xv6fs_type;
  root->op = &xv6fs_op;
  root->parent = NULL;
  root->root = iget(1,1);
  xv6fs_op.read_block(root->root);
  root->mountpoint = 0;
  root->private = kalloc();
  root->op->init();
  root->private = &sb;
}

struct inode*
iget(uint dev, uint inum)//ip 的private 在什么时候kfree？
{
  struct inode *ip, *empty;

  // Is the inode already in the table?
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // Recycle an inode entry.
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->private = NULL;
  return ip;
}

struct inode*
idup(struct inode *ip)
{
  ip->ref++;
  return ip;
}

void
ilock(struct inode *ip)
{
  if(ip == 0 || ip->ref < 1)
    panic("ilock");
  acquiresleep(&ip->lock);
  ip->op->read_block(ip);
}

void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");
  releasesleep(&ip->lock);
}

void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
}

void
iput(struct inode *ip)
{
  if(ip->ref == 1 && ip->private != 0 && ip->nlink == 0){
    // inode has no links and no other references: truncate and free.

    // ip->ref == 1 means no other process can have ip locked,
    // so this acquiresleep() won't block (or deadlock).
    acquiresleep(&ip->lock);

    ip->op->trunc(ip);
    ip->type = 0;
    ip->op->write_inode(ip);
    kfree(ip->private);
    
    releasesleep(&ip->lock);
  }
  ip->ref--;
}

void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

int
dirlink(struct inode *dp, char *name, uint inum)
{
  struct dentry* temp = kalloc();
  temp->inode = dp;
  strncpy(temp->name, name, DIRSIZ);
  temp->private = kalloc();
  *(uint*)(temp->private) = inum;
  int ret = dp->op->link(temp);
  kfree(temp->private);
  kfree(temp);
  return ret;
}

char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip;
  struct dentry *next;

  if(*path == '/')
    ip = root->root;
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // Stop one level early.
      iunlock(ip);
      return ip;
    }
    next = ip->op->dirlookup(ip, name);
    if(next->inode == 0){
      iunlockput(ip);
      kfree(next->private);
      kfree(next);
      return 0;
    }
    iunlockput(ip);
    ip = next->inode;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}

void
fileinit(void)
{
}

struct file*
filealloc(void)
{
  struct file *f;

  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      return f;
    }
  }
  return 0;
}

struct file*
filedup(struct file *f)
{
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  return f;
}

int
filestat(struct file *f, uint64 addr)
{
    struct proc *p = myproc();
    struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE){
    ilock(f->inode);
    stati(f->inode, &st);
    iunlock(f->inode);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
        return -1;
    return 0;
  }
    return -1;
}

int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_DEVICE){
    if(!devsw[1].read)//?????
      return -1;
    r = devsw[1].read(1, addr, n);//hack !!!!!
  }else if(f->type == FD_INODE){
      ilock(f->inode);
      if((r = f->inode->op->read(f->inode, 1, addr, f->off, n)) > 0)
          f->off += r;
      iunlock(f->inode);
  }
  // } else {
  //   panic("fileread");
  // }

  return r;
}

int
filewrite(struct file *f, uint64 addr, int n)
{
    int r, ret = 0;

    if(f->writable == 0)
        return -1;

  if(f->type == FD_DEVICE){
    if(!devsw[1].write)//hack !!!!!
      return -1;
    ret = devsw[1].write(1, addr, n);//hack !!!!!
  } else if(f->type == FD_INODE){
    // write a few blocks at a time to avoid exceeding
    // the maximum log transaction size, including
    // i-node, indirect block, allocation blocks,
    // and 2 blocks of slop for non-aligned writes.
    // this really belongs lower down, since writei()
    // might be writing a device like the console.
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      ilock(f->inode);
      if ((r = f->inode->op->write(f->inode, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->inode);

      if(r != n1){
        // error from writei
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
//   } else {
//     panic("filewrite");
//   }
  }
  return ret;
}