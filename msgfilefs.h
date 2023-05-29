/*
 * Header file containing the stuff needed by the user side (included also in the kernel-side header)
 */
#ifndef MSG_U
	#define MSG_U

	#include <linux/types.h>
	#include <linux/fs.h>



	

	#define MAGIC 0xffff4444
	#define DEFAULT_BLOCK_SIZE 4096
	#define MSGFS_FILE_INODE_NUMBER 1

	#define FILENAME_LEN 8

	#define MSG_FILE_NAME "msg-file"


	#define MASK_SIZE ((MAXBLOCKS - 2) / (sizeof(unsigned int) * 8)) + 1

	

	#define MSGBUF_SIZE ((DEFAULT_BLOCK_SIZE)-sizeof(block_metadata))


	#define INVALIDATE(x) x->bm.invalid=true; 
	#define VALIDATE(x) x->bm.invalid=false;

	#define INITIALIZE(x) \
	INVALIDATE(x) \
	x->bm.msg_size=0; \
	x->bm.tstamp_last=0;\
	memset(x->usrdata, 0, MSGBUF_SIZE);

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
		char filename[FILENAME_LEN];
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









#endif
