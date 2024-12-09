#include "fs/xv6fs/file.h"
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

struct it {
  struct inode inode[NINODE];
} itable;

struct devsw devsw[NDEV];
struct ft {
  struct file file[NFILE];
} ftable;

void iinit() {
  // printf("enter iinit\n");
  for (int i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
  // printf("quit iinit\n");
}

void vfs_init() {
  // printf("enter vfs_init\n");
  root = kalloc();
  root->type = &xv6fs_type;
  root->op = &xv6fs_op;
  root->op->init();
  root->parent = NULL;
  // printf("1111\n");
  root->root = iget(1,1);
  xv6fs_op.read_block(root->root);
  root->mountpoint = 0;
  root->private = kalloc();
  root->private = &sb;
  // printf("quit vfs_init\n");
}

struct inode* iget(uint dev, uint inum) {
  // printf("enter iget\n");
  struct inode *ip, *empty = 0;
  for (ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++) {
    if (ip->ref > 0 && ip->dev == dev && ip->inum == inum) {
      ip->ref++;
      // printf("quit iget\n");
      return ip;
    }
    if (empty == 0 && ip->ref == 0)
      empty = ip;
  }
  if (empty == 0)
    panic("iget: no inodes");
  ip = empty;
  ip->dev = dev;
  // printf("dev:%d\n", dev);
  ip->op = &xv6fs_op;
  ip->inum = inum;
  ip->ref = 1;
  ip->private = NULL;
  // printf("quit iget\n");
  return ip;
}

struct inode* idup(struct inode *ip) {
  // printf("enter idup\n");
  ip->ref++;
  // printf("quit idup\n");
  return ip;
}

void ilock(struct inode *ip) {
  // printf("enter ilock\n");
  if (ip == 0 || ip->ref < 1)
    panic("ilock");
  acquiresleep(&ip->lock);
  ip->op->read_block(ip);
  // printf("quit ilock\n");
}

void   iunlock(struct inode *ip) {
  // printf("enter iunlock\n");
  if (ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");
  releasesleep(&ip->lock);
  // printf("quit iunlock\n");
}

void stati(struct inode *ip, struct stat *st) {
  // printf("enter stati\n");
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->size = ip->size;
  // printf("quit stati\n");
}

void iput(struct inode *ip) {
  // printf("enter iput\n");
  if (ip->ref == 1 && ip->private != 0 && ip->nlink == 0) {
    acquiresleep(&ip->lock);
    ip->op->trunc(ip);
    ip->type = 0;
    ip->op->write_inode(ip);
    kfree(ip->private);
    releasesleep(&ip->lock);
  }
  ip->ref--;
  // printf("quit iput\n");
}

void iunlockput(struct inode *ip) {
  // printf("enter iunlockput\n");
  iunlock(ip);
  iput(ip);
  // printf("quit iunlockput\n");
}

int namecmp(const char *s, const char *t) {
  // printf("enter namecmp\n");
  int result = strncmp(s, t, DIRSIZ);
  // printf("quit namecmp\n");
  return result;
}

int dirlink(struct inode *dp, char *name, uint inum) {
  // printf("enter dirlink\n");
  struct dentry* temp = kalloc();
  temp->inode = dp;
  strncpy(temp->name, name, DIRSIZ);
  temp->private = kalloc();
  *(uint*)(temp->private) = inum;
  int ret = dp->op->link(temp);
  kfree(temp->private);
  kfree(temp);
  // printf("quit dirlink\n");
  return ret;
}

char* skipelem(char *path, char *name) {
  // printf("enter skipelem\n");
  char *s;
  int len;
  while (*path == '/') path++;
  if (*path == 0) {
    // printf("quit skipelem\n");
    return 0;
  }
  s = path;
  while (*path != '/' && *path != 0) path++;
  len = path - s;
  if (len >= DIRSIZ) memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while (*path == '/') path++;
  // printf("quit skipelem\n");
  return path;
}

struct inode* namex(char *path, int nameiparent, char *name) {
  // printf("enter namex\n");
  struct inode *ip;
  struct dentry *next;
  if (*path == '/') {
    if (root == NULL) {
      ip = iget(1, 1);
    } else {
      ip = root->root;
    }
  } else {
    ip = idup(myproc()->cwd);
  }
  while ((path = skipelem(path, name)) != 0) {
    ilock(ip);
    if (ip->type != T_DIR) {
      iunlockput(ip);
      // printf("quit namex\n");
      return 0;
    }
    if (nameiparent && *path == '\0') {
      iunlock(ip);
      // printf("quit namex\n");
      return ip;
    }
    next = ip->op->dirlookup(ip, name);
    if (next->inode == 0) {
      iunlockput(ip);
      kfree(next->private);
      kfree(next);
      // printf("quit namex\n");
      return 0;
    }
    iunlockput(ip);
    ip = next->inode;
  }
  if (nameiparent) {
    iput(ip);
    // printf("quit namex\n");
    return 0;
  }
  // printf("quit namex\n");
  return ip;
}

struct inode* namei(char *path) {
  // printf("enter namei\n");
  char name[DIRSIZ];
  // printf("quit namei\n");
  return namex(path, 0, name);
}

struct inode* nameiparent(char *path, char *name) {
  // printf("enter nameiparent\n");
  // printf("quit nameiparent\n");
  return namex(path, 1, name);
}

void fileinit(void) {
  // printf("enter fileinit\n");
  // Initialization code here
  // printf("quit fileinit\n");
}

struct file* filealloc(void) {
  // printf("enter filealloc\n");
  struct file *f;
  for (f = ftable.file; f < ftable.file + NFILE; f++) {
    if (f->ref == 0) {
      f->ref = 1;
      // printf("quit filealloc\n");
      return f;
    }
  }
  // printf("quit filealloc\n");
  return 0;
}

struct file* filedup(struct file *f) {
  // printf("enter filedup\n");
  if (f->ref < 1) {
    panic("filedup");
  }
  f->ref++;
  // printf("quit filedup\n");
  return f;
}

int filestat(struct file *f, uint64 addr) {
  // printf("enter filestat\n");
  struct proc *p = myproc();
  struct stat st;

  if (f->type == FD_INODE || f->type == FD_DEVICE) {
    ilock(f->inode);
    stati(f->inode, &st);
    iunlock(f->inode);
    if (copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0) {
      // printf("quit filestat\n");
      return -1;
    }
    // printf("quit filestat\n");
    return 0;
  }
  // printf("quit filestat\n");
  return -1;
}

int fileread(struct file *f, uint64 addr, int n) {
  // printf("enter fileread\n");
  int r = 0;

  if (f->readable == 0) {
    // printf("quit fileread\n");
    return -1;
  }

  if (f->type == FD_DEVICE) {
    r = devsw[CONSOLE].read(1, addr, n);
  } else if (f->type == FD_INODE) {
    ilock(f->inode);
    if ((r = f->inode->op->read(f->inode, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->inode);
  }
  // printf("quit fileread\n");
  return r;
}

int filewrite(struct file *f, uint64 addr, int n) {
  // printf("enter filewrite\n");
  int r, ret = 0;

  if (f->writable == 0) {
    // printf("quit filewrite\n");
    return -1;
  }

  if (f->type == FD_DEVICE) {
    ret = devsw[CONSOLE].write(1, addr, n);
  } else if (f->type == FD_INODE) {
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while (i < n) {
      int n1 = n - i;
      if (n1 > max)
        n1 = max;

      ilock(f->inode);
      if ((r = f->inode->op->write(f->inode, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->inode);

      if (r != n1)
        break;
      i += r;
    }
    ret = (i == n ? n : -1);
  }
  // printf("quit filewrite\n");
  return ret;
}