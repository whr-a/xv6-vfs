#pragma once

#include "fs.h"
#include "sleeplock.h"
#include "types.h"

struct xv6fs_file {
  // int ref; // reference count
  // char readable;
  // char writable;
  // struct pipe *pipe; // FD_PIPE
  // struct xv6fs_inode *ip;  // FD_INODE and FD_DEVICE
  // uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// in-memory copy of an inode
struct xv6fs_inode {
  uint dev;           // Device number
  // uint inum;          // Inode number
  // int ref;            // Reference count
  // struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  // short type;         // copy of disk inode
  short major;
  short minor;
  // short nlink;
  // uint size;
  uint addrs[NDIRECT+1];
};

#define CONSOLE 1
