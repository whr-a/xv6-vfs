// //
// // Support functions for system calls that involve file descriptors.
// //

// #include "types.h"
// #include "riscv.h"
// #include "kernel/defs.h"
// #include "defs.h"
// #include "param.h"
// #include "buf.h"
// #include "fs.h"
// #include "spinlock.h"
// #include "sleeplock.h"
// #include "file.h"
// #include "stat.h"
// #include "proc.h"
// #include "fs/vfs.h"
// #include "fs/vfs_file.h"

// struct devsw devsw[NDEV];

// void
// xv6fs_fileclose(struct file *f)
// {
//   struct file ff;

//   if(f->ref < 1)
//     panic("fileclose");
//   if(--f->ref > 0){
//     return;
//   }
//   ff = *f;
//   f->ref = 0;
//   f->type = FD_NONE;

//   // if(ff.type == FD_PIPE){
//   //   pipeclose(ff.pipe, ff.writable);
//   // } else 
//   if(ff.type == FD_INODE || ff.type == FD_DEVICE){
//     iput(ff.inode);
//   }
// }
