#ifndef _COFS_COMMON_H
#define _COFS_COMMON_H

#ifndef COFS_BLOCK_SIZE
#define COFS_BLOCK_SIZE 512
#endif

#define COFS_MAGIC 0xC0517155       // cosiris FS magic number

// SuperBlock
typedef struct cofs_superblock
{
    unsigned int magic;
    unsigned int size;              // total size of fs, in blocks
    unsigned int num_blocks;        // number of data blocks
    unsigned int num_inodes;        // number of inodes
    unsigned int bitmap_start;      // where free bitmap starts
    unsigned int inode_start;       // where inodes starts
                                    // TODO: add where data blocks starts
                                    // so we ensure we never allocate blocks
                                    // in bitmap or inoode zone
} cofs_superblock_t;


/**
 * In an inode, to define data, we keep the track of allocated blocks of data;
 * We keep the block index as an unsigned int. That's it, we can allocate maximum of 
 * 2^32 blocks of COFS_BLOCK_SIZE in out filesystem ~ 2 TB limitation
 * We have direct allocated blocks of data, 
 * simple indirect - a block keeping pointers to allocated data
 * double indirect - a block keeping pointers to blocks of pointers containing data
 * - TODO - triple indirect
 *   So, the size limit of a file will be COFS_BLOCK_SIZE * (NUM_DIRECT + NUM_SIND + NUM_DIND)
 */
#define NUM_DIRECT  6                           // number of direct blocks in the inode
#define NUM_SIND    (COFS_BLOCK_SIZE / sizeof(int))  // number of single indirect blocks
#define NUM_DIND    (NUM_SIND * NUM_SIND)       // number of double indidrect blocks
#define NUM_EINB    (COFS_BLOCK_SIZE / sizeof(int)) // number of block entries in a block

// max file size in blocks ~ 8 MB if COFS_BLOCK_SIZE == 512
#define MAX_FILE_SIZE (NUM_DIRECT + NUM_SIND + NUM_DIND)

// The inode struct
// Must be % COFS_BLOCK_SIZE
typedef struct cofs_inode {
    unsigned short int type;            // type of inode - file, link, directory, etc. type is mode and mode is type :D
    unsigned short int major;           // for devices, major, minor
    unsigned short int minor;
    unsigned short int uid;             // user id
    unsigned short int gid;             // group id
    unsigned short int num_links;       // number of links to this inode, (aka ln -s)
    unsigned int atime, mtime, ctime;   // infos about time, not really used now
    unsigned int size;                  // file size
    // Space for direct blocks to data, SIND_IDX, DIND_IDX 
    // + 1 reserved for future triple indx
    unsigned int addrs[NUM_DIRECT + 3];
} cofs_inode_t;

// index into inode addrs to single indirect block
#define SIND_IDX    NUM_DIRECT

// index into inode addrs to double indirect block
#define DIND_IDX    NUM_DIRECT + 1

// number of inodes that can fit into a block of COFS_BLOCK_SIZE //
#define NUM_INOPB (COFS_BLOCK_SIZE / sizeof(cofs_inode_t))

// number of bits a block  of COFS_BLOCK_SIZE has - used in bitmap
#define BITS_PER_BLOCK (COFS_BLOCK_SIZE * 8)

// block witch contains inode n
#define INO_BLOCK(ino, superblock)    ((ino) / NUM_INOPB + superblock.inode_start)

// block of bitmap containing bit for block b //
#define BITMAP_BLOCK(block, superblock) (block / BITS_PER_BLOCK + superblock->bitmap_start)

#define COFS_FILE_NAME_MAX_LEN 28
// a structure that describe a filename in a directory
struct cofs_dirent {
    unsigned int d_ino;
    char d_name[COFS_FILE_NAME_MAX_LEN];
};

#ifndef cofs_min
    #define cofs_min(a, b) ((a) < (b) ? (a) : (b))
#endif

#endif // _COFS_COMMON_H
