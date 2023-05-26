
#include "msgfilefs.h"

struct priv_info {
	uint64_t nblocks;
	loff_t file_size;
	unsigned int inv_bitmask [ MASK_SIZE ];
	struct mutex write_mt;
	//struct block_order_node * list;		//with rcu, probably useless
	struct list_head bm_list;
};

/*
struct s_priv_info{
	time64_t timestamp;
	int offset;
}*/

//with rcu, probably useless
struct block_order_node {
	block_metadata bm;
	struct list_head bm_list;
};

extern bool mounted;