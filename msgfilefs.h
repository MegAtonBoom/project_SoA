#ifndef MSG_H
#define MSG_H

#include <linux/types.h>
#include <linux/fs.h>



#define TEST


#define MODNAME "MSG FILE FS"

#define ERROR -1

#define MAGIC 0xffff4444
#define DEFAULT_BLOCK_SIZE 4096
#define SB_BLOCK_NUMBER 0
#define DEFAULT_FILE_INODE_BLOCK 1

#define FILENAME_MAXLEN 8

#define MSGFS_ROOT_INODE_NUMBER 10
#define MSGFS_FILE_INODE_NUMBER 1

#define MSGFS_INODES_BLOCK_NUMBER 1

#define MSG_FILE_NAME "msg-file"

#define INVALIDATE(x) x->bm.invalid=true; 
#define VALIDATE(x) x->bm.invalid=false;

#define MASK_SIZE ((MAXBLOCKS - 2) / (sizeof(unsigned int) * 8)) + 1

#define INITIALIZE(x) \
INVALIDATE(x) \
x->bm.msg_size=0; \
x->bm.tstamp_last=0;\
memset(x->usrdata, 0, MSGBUF_SIZE);

#ifdef TEST 
	#define TESTING(x) \
	  x
#else
	#define TESTING(x) 
#endif

extern int max_nblocks;
//extern int datablocks;
extern int datablocks;

//#define BM_SIZE (datablocks + (sizeof(int)-1))/ datablocks

#define MSGBUF_SIZE ((DEFAULT_BLOCK_SIZE)-sizeof(block_metadata))

#define HACKED_ENTRIES 3


//inode definition
struct __attribute__((packed)) msgfs_inode {
	//mode_t mode;//not exploited
	uint64_t inode_no;
	//uint64_t data_block_number;//not exploited

	union {
		uint64_t file_size;
		uint64_t dir_children_count;
	};
	char padding[ (DEFAULT_BLOCK_SIZE) - (2 * sizeof(uint64_t))];
};

//dir definition (how the dir datablock is organized)
struct msgfs_dir_record {
	char filename[FILENAME_MAXLEN];
	uint64_t inode_no;
};


//superblock definition
struct __attribute__((packed)) msgfs_sb_info {
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
	//uint64_t nblocks;
	//uint64_t inodes_count;//not exploited
	//uint64_t free_blocks;//not exploited

	//padding to fit into a single block
	char padding[ (DEFAULT_BLOCK_SIZE) - (3 * sizeof(uint64_t))];
};

typedef struct msgfs_metadata {
	int offset;
	unsigned long tstamp_last;  //timestamp of the last modify (write or invalidate)
	bool invalid;	
	int msg_size;

} block_metadata;


//datablock definition
typedef struct __attribute__((packed)) msgfs_block_data {
	block_metadata bm;
	char usrdata[MSGBUF_SIZE];
} data_block;







// file.c
extern const struct inode_operations msgfilefs_inode_ops;
extern const struct file_operations msgfilefs_file_operations; 

// dir.c
extern const struct file_operations msgfilefs_dir_operations;

//the (unique) superblock to be referenced by the module system calls
extern struct super_block *the_sb;


//functions that modifies syscall table entries with the new ones
extern int hack_syscall_table(void);
extern void unhack_syscall_table(void);

//syscall "hacking" related stuff
extern unsigned long the_syscall_table;
extern unsigned long the_ni_syscall;
extern unsigned long new_sys_call_array[];
extern int restore[];
extern int hacked_entries;


//added bitmask to improve the invalid block selection
extern unsigned int inv_bitmask[];

extern void initBit(unsigned int[]);
extern int setBit(unsigned int [], int , bool );
extern int getBit(unsigned int [], int );
extern int getInvBit(unsigned int []);





#endif
