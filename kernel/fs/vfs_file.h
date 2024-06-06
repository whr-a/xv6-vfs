// //
// // Support functions for system calls that involve file descriptors.
// //

// #include "types.h"
// #include "riscv.h"
// #include "kernel/defs.h"
// #include "defs.h"
// #include "param.h"
// #include "buf.h"
// #include "spinlock.h"
// #include "sleeplock.h"
// #include "stat.h"
// #include "proc.h"
// #include "vfs.h"
// #include "vfs_fs.h"

// void fileinit(void);

// // Allocate a file structure.
// struct file*
// filealloc(void);

// // Increment ref count for file f.
// struct file*
// filedup(struct file *f);

// // #define T_DIR     1   // Directory
// // #define T_FILE    2   // File
// // #define T_DEVICE  3   // Device

// // enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;

// int
// filestat(struct file *f, uint64 addr);

// // Read from file f.
// // addr is a user virtual address.
// int
// fileread(struct file *f, uint64 addr, int n);

// // Write to file f.
// // addr is a user virtual address.
// int
// filewrite(struct file *f, uint64 addr, int n);