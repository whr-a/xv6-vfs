#pragma once

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "vfs.h"
#define min(a, b) ((a) < (b) ? (a) : (b))

//file.c
void fileinit(void);
struct file* filealloc(void);
struct file* filedup(struct file *);
int filestat(struct file *, uint64);
int fileread(struct file *, uint64, int);
int filewrite(struct file *, uint64, int);

//fs.c
void iinit();
struct inode* iget(uint, uint);
void vfs_init();
struct inode* idup(struct inode *);
void ilock(struct inode *);
void iunlock(struct inode *);
void stati(struct inode *, struct stat *);
void iput(struct inode *);
void iunlockput(struct inode *);
int dirlink(struct inode *, char *, uint);
char* skipelem(char *, char *);
struct inode* namex(char *, int, char *);
struct inode* namei(char *);
struct inode* nameiparent(char *, char *);