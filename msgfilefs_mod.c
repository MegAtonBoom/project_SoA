#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/syscalls.h>
#include <linux/stddef.h>
#include <linux/rculist.h>

#include "msgfilefs_kernel.h"



static struct super_operations singlefilefs_super_ops = {
};


static struct dentry_operations singlefilefs_dentry_ops = {
};


int datablocks;

struct super_block *the_sb;

bool mounted;



unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);


unsigned long the_ni_syscall;

unsigned long new_sys_call_array[] = {0x0, 0x0, 0x0};//please set to sys_put_work at startup
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};


int msgfs_fill_super(struct super_block *sb, void *data, int silent) {   

    struct inode *root_inode;
    struct buffer_head *bh;
    struct msgfs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;
    struct priv_info *pi;
    struct block_order_node * block=NULL, * prev=NULL, * curr=NULL;
    struct msgfs_inode *my_inode;
    data_block *db;
    bool first = true;
    int i, j;
    

    //Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if(!bh){
        //TESTING(printk("Error in bread in fill_superblock\n"));
	    return -EIO;
    }
    sb_disk = (struct msgfs_sb_info *)bh->b_data;
    magic = sb_disk->magic;
    brelse(bh);

    //check on the expected magic number
    if(magic != sb->s_magic){
        //TESTING(printk("Error with the magic number!\n"));
	    return -EBADF;
    }

    /*if(sb_disk->nblocks>MAXBLOCKS){
        return -EPERM;
    }*/
    pi=(struct priv_info *)kzalloc(sizeof(struct priv_info), GFP_KERNEL);
    if(pi==NULL){
        printk(KERN_INFO "%s: Error allocating memory\n",MODNAME);
        return -ENOMEM;
    }

    //pi->list = head;
    //pi->nblocks = sb_disk->nblocks;
    initBit(pi->inv_bitmask);
    //pi->inv_bitmask = {0};
    mutex_init((&pi->write_mt));
    INIT_LIST_HEAD_RCU(&(pi->bm_list));
    bh = sb_bread(sb, 1);
    if(!bh){
            return -EIO;
    }
    sb->s_fs_info=(void *)pi;
    my_inode = (struct msgfs_inode *)bh->b_data;
    if(my_inode->file_size>MAXBLOCKS){
        return -EPERM;
    }
    pi->file_size = my_inode->file_size;
    TESTING(printk("size is %d!\n", pi->file_size));
    datablocks = pi->file_size;
    //TESTING(printk("Actual datablocks are %d\n", datablocks));
    for(i=2; i< pi->file_size+2; i++){
        TESTING(printk("Iterazione %d, fino a %d", i, pi->file_size + 2));
        bh = sb_bread(sb, i);
        if(!bh){
            brelse(bh);
            return -EIO;
        }
        db = (data_block * )bh->b_data;

        if(db->bm.invalid){
            TESTING(printk("found block at index %d invalid: writing in bitmask at pos %d\n", i, db->bm.offset-2));
            //pi->inv_bitmask[(i-2) / (sizeof(unsigned int) * 8)] |= (1ul >> ( (i-2) % (sizeof(unsigned int) * 8)));
            setBit(pi->inv_bitmask, db->bm.offset-2, true);
            
            //TESTING(printk("wrote true at %d", db->bm.offset-2));
            //TESTING(printk("now the bismask value is %u" , pi->inv_bitmask[0]));
            continue;
        }
        TESTING(printk("The block is valid! offset: %d, valid: %d\n", db->bm.offset, db->bm.invalid));
        brelse(bh);
        block = (struct block_order_node *)kzalloc(sizeof(struct block_order_node), GFP_KERNEL);
        if(block==NULL){
            //TESTING(printk(KERN_INFO "%s: Error allocating memory\n",MODNAME));
            return -ENOMEM;
        }
        block->bm.offset = db->bm.offset;
        block->bm.tstamp_last = db->bm.tstamp_last;
        block->bm.invalid = db->bm.invalid;
        block->bm.msg_size = db->bm.msg_size;

        prev = list_first_or_null_rcu(&(pi->bm_list), struct block_order_node, bm_list);
        if(!prev || (block->bm.tstamp_last < prev->bm.tstamp_last)){
            TESTING(printk("Block with offset %d is added first\n",block->bm.offset));
            list_add_rcu(&(block->bm_list), &(pi->bm_list));
            continue;
        }
        curr = list_next_or_null_rcu(&(pi->bm_list), &(prev->bm_list), struct block_order_node, bm_list);
        if(!curr || (block->bm.tstamp_last < curr->bm.tstamp_last)){
            TESTING(printk("Block with offset %d added second\n",block->bm.offset));
            list_add_rcu(&(block->bm_list), &(prev->bm_list));
            continue;
        }
        for(j=0; j<datablocks; j++){
            prev = curr;
            curr = list_next_or_null_rcu(&(pi->bm_list), &(curr->bm_list), struct block_order_node, bm_list);
            if(!curr || (block->bm.tstamp_last < curr->bm.tstamp_last)){
                list_add_rcu(&(block->bm_list), &(prev->bm_list));
                TESTING(printk("Block added in %d position\n", j+2));
                break;
            }
        
        }
    }
    

    sb->s_op = &singlefilefs_super_ops;//set our own operations
    the_sb=sb;

    root_inode = iget_locked(sb, 0);//get a root inode indexed with 0 from cache
    if (!root_inode){
        return -ENOMEM;
    }

    root_inode->i_ino = SINGLEFILEFS_ROOT_INODE_NUMBER;//this is actually 10
    //5.12
    inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR);//set the root user as owned of the FS root
    root_inode->i_sb = sb;
    root_inode->i_op = &msgfilefs_inode_ops;//set our inode operations
    root_inode->i_fop = &msgfilefs_file_operations;
    //root_inode->i_fop = &onefilefs_dir_operations;//set our file operations
    //update access permission
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    //baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    sb->s_root = d_make_root(root_inode);
    if (!sb->s_root)
        return -ENOMEM;

    sb->s_root->d_op = &singlefilefs_dentry_ops;//set our dentry operations

    //unlock the inode to make it usable
    unlock_new_inode(root_inode);

    return 0;
}


static void msgfs_kill_superblock(struct super_block *sb) {

    struct block_order_node *curr=NULL, *prev = NULL;
    struct priv_info *pi = (struct priv_info *)sb->s_fs_info;
    bool empty_list = true;

    
    TESTING(printk("UNMOUNT\n"));
    list_for_each_entry_rcu(curr, &(pi->bm_list), bm_list){
        if(!prev){
            empty_list = false;
            TESTING(printk("FIRST ELEM: frkeeping track of %lu\n", curr));
            prev = curr;
        }
        else{
            TESTING(printk("NOW FREEING %lu, next elem: %lu\n", prev, curr));
            kfree(prev);
            prev = curr;
        }
        
    }
    //no elem in list
    if(!empty_list){
        TESTING(printk("LIST IS NOT EMPTY SO:DELETING LAST ITEM AT %lu\n", prev));
        kfree(prev);
    }
    TESTING(printk("NOW FREEING S_FS_INFO AT %lu",sb->s_fs_info));
    kfree(sb->s_fs_info);
    
    if(unlikely(!__sync_bool_compare_and_swap(&mounted, true, false))){
        printk("Error in mount val management\n");
    }

    kill_block_super(sb);

    printk(KERN_INFO "%s: filesystem unmount succesful.\n",MODNAME);
    return;
}

//called on file system mounting 
struct dentry *msgfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    struct dentry *ret;
    if(unlikely(!__sync_bool_compare_and_swap(&mounted, false, true))){
        printk("Device already mounted: shutting down");
        return ERR_PTR(-EPERM);
    }
    ret = mount_bdev(fs_type, flags, dev_name, data, msgfs_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting msgfilefs",MODNAME);
    else
        printk("%s: msgfilefs is succesfully mounted on from device %s\n",MODNAME,dev_name);

    return ret;
}

//file system structure
static struct file_system_type onefilefs_type = {
	.owner = THIS_MODULE,
        .name           = "msgfilefs",
        .mount          = msgfs_mount,
        .kill_sb        = msgfs_kill_superblock,
};




static int msgfilefs_init(void) {

    int ret;
    mounted = false;
    if(unlikely(hack_syscall_table()!=0)){
        printk("Error in syscall table hacking");
        return -1;
    }
        
    

    //register filesystem
    ret = register_filesystem(&onefilefs_type);
    if (likely(ret == 0)){
        printk("%s: sucessfully registered singlefilefs\n",MODNAME);
    }
    else{
        printk("%s: failed to register singlefilefs - error %d", MODNAME,ret);
    }
    
    return 0;
}

static void msgfilefs_exit(void) {

    int ret;
    unhack_syscall_table();
    
    //unregister filesystem
    ret = unregister_filesystem(&onefilefs_type);

    if (likely(ret == 0))
        printk("%s: sucessfully unregistered file system driver\n",MODNAME);
    else
        printk("%s: failed to unregister singlefilefs driver - error %d", MODNAME, ret);
        
}

module_init(msgfilefs_init);
module_exit(msgfilefs_exit);




MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adrian Petru Baba");
MODULE_DESCRIPTION("USER-MESSAGE-FS");
