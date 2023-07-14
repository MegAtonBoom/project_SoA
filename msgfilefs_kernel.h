/*
 * Header file cotaining the stuff needed by the kernel side 
 */
 
#ifndef MSG_K 
	#define MSG_K

	#include <linux/srcu.h>
	#include "msgfilefs.h"

	#define HACKED_ENTRIES 3

	#define MODNAME "MSG FILE FS"

	#define ERROR -1

	#define SB_BLOCK_NUMBER 0
	#define DEFAULT_FILE_INODE_BLOCK 1
	#define MSGFS_INODES_BLOCK_NUMBER 1

	#define MSGFS_ROOT_INODE_NUMBER 10


	struct priv_info {
		uint64_t nblocks;
		loff_t file_size;
		unsigned int inv_bitmask [ MASK_SIZE ];
		struct mutex write_mt;
		struct list_head bm_list;
		struct srcu_struct srcu;
	};

	struct block_order_node {
		rcu_metadata bm;
		struct list_head bm_list;
	};

	extern bool mounted;

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

	extern int max_nblocks;
	extern int datablocks;

	//used to correctly do the cleanup of the srcu, as documented at lwn.net/Articles/202847/
	extern int stop_rcu;


	//added bitmask to improve the invalid block selection
	extern unsigned int inv_bitmask[];

	extern void initBit(unsigned int[]);
	extern int setBit(unsigned int [], int , bool );
	extern int getBit(unsigned int [], int );
	extern int getInvBit(unsigned int []);
	extern void concatenate_bytes(char *, size_t, char *, size_t);


#endif
