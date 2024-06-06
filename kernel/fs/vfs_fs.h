// // File system implementation.  Five layers:
// //   + Blocks: allocator for raw disk blocks.
// //   + Log: crash recovery for multi-step updates.
// //   + Files: inode allocator, reading, writing, metadata.
// //   + Directories: inode with special contents (list of other inodes!)
// //   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
// //
// // This file contains the low-level file system manipulation
// // routines.  The (higher-level) system call implementations
// // are in sysfile.c.

// #include "types.h"
// #include "riscv.h"
// #include "kernel/defs.h"
// #include "defs.h"
// #include "param.h"
// #include "stat.h"
// #include "spinlock.h"
// #include "proc.h"
// #include "sleeplock.h"
// #include "buf.h"
// #include "vfs.h"
// #include "xv6fs/fs.h"
// #include <time.h>
// #define min(a, b) ((a) < (b) ? (a) : (b))
// // struct super_block *root;

// // struct it{
// //   struct inode inode[NINODE];
// // } itable;

// void iinit();

// struct inode* iget(uint dev, uint inum);
// void vfs_init();
// struct inode* idup(struct inode *ip);
// // Lock the given inode.
// // Reads the inode from disk if necessary.
// void ilock(struct inode *ip);

// // Unlock the given inode.
// void iunlock(struct inode *ip);

// // Copy stat information from inode.
// // Caller must hold ip->lock.
// void stati(struct inode *ip, struct stat *st);

// // Drop a reference to an in-memory inode.
// // If that was the last reference, the inode table entry can
// // be recycled.
// // If that was the last reference and the inode has no links
// // to it, free the inode (and its content) on disk.
// // All calls to iput() must be inside a transaction in
// // case it has to free the inode.
// void iput(struct inode *ip);

// // Common idiom: unlock, then put.
// void iunlockput(struct inode *ip);

// // Write a new directory entry (name, inum) into the directory dp.
// // Returns 0 on success, -1 on failure (e.g. out of disk blocks).
// int dirlink(struct inode *dp, char *name, uint inum);

// // Paths

// // Copy the next path element from path into name.
// // Return a pointer to the element following the copied one.
// // The returned path has no leading slashes,
// // so the caller can check *path=='\0' to see if the name is the last one.
// // If no name to remove, return 0.
// //
// // Examples:
// //   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
// //   skipelem("///a//bb", name) = "bb", setting name = "a"
// //   skipelem("a", name) = "", setting name = "a"
// //   skipelem("", name) = skipelem("////", name) = 0
// //
// char* skipelem(char *path, char *name);

// // Look up and return the inode for a path name.
// // If parent != 0, return the inode for the parent and copy the final
// // path element into name, which must have room for DIRSIZ bytes.
// // Must be called inside a transaction since it calls iput().
// struct inode* namex(char *path, int nameiparent, char *name);

// struct inode* namei(char *path);

// struct inode* nameiparent(char *path, char *name);
