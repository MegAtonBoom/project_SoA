/*
 *  This file cointains device related stuff like mounting function, unmounting functiom
 *  and the "fill superblock" function and module related stuff, in particular init and
 *  exit functions, wich only have to hack and unhack the syscall table with the new system calls
 *  defined in "syscall.c" file
 */ 


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
#include <linux/srcu.h>

#include "msgfilefs_kernel.h"



static struct super_operations msgfilefs_super_ops = {
};


static struct dentry_operations msgfilefs_dentry_ops = {
};


int datablocks;

int stop_rcu;

struct super_block *the_sb;

//for simplicity we assume our device can be mounted only once
bool mounted;



//stuff related to the syscall table hack 
unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);


unsigned long the_ni_syscall;

unsigned long new_sys_call_array[] = {0x0, 0x0, 0x0};
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};

//the function that fills the superblock metadata stored in memory
int msgfs_fill_super(struct super_block *sb, void *data, int silent) {   

    struct inode *root_inode;
    struct buffer_head *bh;
    struct msgfs_sb_info *sb_disk;
    struct timespec64 curr_time;
    uint64_t magic;

    struct priv_info *pi;
    struct block_order_node * block=NULL;
    struct msgfs_inode *my_inode;
    int i;
    
    stop_rcu = 0;
    //Unique identifier of the filesystem
    sb->s_magic = MAGIC;

    //readign disk superblock
    bh = sb_bread(sb, SB_BLOCK_NUMBER);
    if(!bh){
	    return -EIO;
    }

    sb_disk = (struct msgfs_sb_info *)bh->b_data;
    magic = sb_disk->magic;

    //private info, our file system specific metadata kept in ram
    pi=(struct priv_info *)kzalloc(sizeof(struct priv_info), GFP_KERNEL);
    if(!pi){
        printk(KERN_INFO "%s: Error allocating memory\n",MODNAME);
        return -ENOMEM;
    }

    mutex_init((&pi->write_mt));
    init_srcu_struct(&pi->srcu);
    INIT_LIST_HEAD_RCU(&(pi->bm_list));
    sb->s_fs_info=(void *)pi;

    initBit(pi->inv_bitmask);

    //for every valid block we have its index saved in the superblock; we just read the info
    //from the superblock and make easily the ordered rcu list of metadata
    for(i=0; i < sb_disk->n_valid_blocks; i++){
        block = (struct block_order_node *)kzalloc(sizeof(struct block_order_node), GFP_KERNEL);

        if(!block){
            printk(KERN_INFO "%s: Error allocating memory\n", MODNAME);
            return -ENOMEM;
        }
        setBit(pi->inv_bitmask, sb_disk->valid_blocks[i]-2, false);
        block->bm.offset = sb_disk->valid_blocks[i];
        block->bm.tstamp_last = i;

        list_add_tail_rcu(&(block->bm_list), &(pi->bm_list));
    }
    brelse(bh);

    //check on the expected magic number
    if(magic != sb->s_magic){
        printk(KERN_INFO "%s: Not supported magic number\n",MODNAME);
	    return -EBADF;
    }

    
    sb->s_op = &msgfilefs_super_ops;    //set our own operations
    
    

    root_inode = iget_locked(sb, 0);    //get a root inode indexed with 0 from cache
    if (!root_inode){
        printk(KERN_INFO "%s: Error in getting the inode\n",MODNAME);
        return -ENOMEM;
    }

    root_inode->i_ino = MSGFS_ROOT_INODE_NUMBER;

    #if LINUX_VERSION_CODE >= KERNEL_VERSION(5,12,0)
        inode_init_owner(&init_user_ns, root_inode, NULL, S_IFDIR);
    #else
        inode_init_owner(root_inode, NULL, S_IFDIR);
    #endif

    root_inode->i_sb = sb;

    //attaching our own operations
    root_inode->i_op = &msgfilefs_inode_ops;
    root_inode->i_fop = &msgfilefs_dir_operations;
    
    root_inode->i_mode = S_IFDIR | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;

    //baseline alignment of the FS timestamp to the current time
    ktime_get_real_ts64(&curr_time);
    root_inode->i_atime = root_inode->i_mtime = root_inode->i_ctime = curr_time;

    // no inode from device is needed - the root of our file system is an in memory object
    root_inode->i_private = NULL;

    sb->s_root = d_make_root(root_inode);

    if (!sb->s_root){
        printk(KERN_INFO "%s: Error in getting the inode\n",MODNAME);
        return -ENOMEM;
    }

    sb->s_root->d_op = &msgfilefs_dentry_ops;
    //unlock the inode to make it usable
    unlock_new_inode(root_inode);
    
    //reading our own inode
    bh = sb_bread(sb, 1);
    if(!bh){
            printk(KERN_INFO "%s: Error with the bread\n",MODNAME);
            return -EIO;
    }
    
    my_inode = (struct msgfs_inode *)bh->b_data;

    //checking file size- we assume we have only one file and the max size of the file system
    //cant be > MAXBLOCKSn (-2 because we don't take in account superblock and inode)
    if(my_inode->file_size>MAXBLOCKS-2){
        brelse(bh);
        printk(KERN_INFO "%s: Error: the device has more blocks than supported\n",MODNAME);
        return -EPERM;
    }

    pi->file_size = my_inode->file_size;
    datablocks = pi->file_size;
    brelse(bh);


    the_sb=sb;
    return 0;
}

//the function that kills the superblock, fir of all it just frees all the memory used in the
//private info structure, including the RCU list, saving the ordered offsets in the superblock
static void msgfs_kill_superblock(struct super_block *sb) {

    struct block_order_node *curr=NULL, *prev = NULL;
    struct priv_info *pi;
    struct srcu_struct *srcu;
    struct buffer_head *bh;
    struct msgfs_sb_info *the_sb;
    bool empty_list = true;
    int nvalid_blocks = 0;


    pi = (struct priv_info *)sb->s_fs_info;
    srcu = &pi->srcu;

    bh = sb_bread(sb, 0);  
    if(!bh){
        brelse(bh);
        printk(KERN_INFO "%s: Error with the bread\n",MODNAME);
        return;
    }
    the_sb = (struct msgfs_sb_info *)bh->b_data;

    //for each element
    list_for_each_entry_rcu(curr, &(pi->bm_list), bm_list){
        //if we are at the first element, we just keep track of him
        if(!prev){
            empty_list = false;
           
            prev = curr;
        }
        //if we are not, we free the previous one
        else{
            the_sb->valid_blocks[nvalid_blocks] = prev->bm.offset;
            nvalid_blocks++;
            kfree(prev);
            prev = curr;
        }
        
    }
    //Note: if the list is empy we DON'T have to free anything (except for the structure itself)
    if(!empty_list){
        the_sb->valid_blocks[nvalid_blocks] = prev->bm.offset;
        nvalid_blocks++;
        kfree(prev);
    }
    the_sb->n_valid_blocks = nvalid_blocks;
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    
    //srcu cleanup phase
    stop_rcu = 1;
    synchronize_rcu();
    synchronize_srcu(srcu);
    cleanup_srcu_struct(srcu);
    kfree(sb->s_fs_info);
    
    if(unlikely(!__sync_bool_compare_and_swap(&mounted, true, false))){
        printk(KERN_INFO "%s: Error in mounted variable management\n",MODNAME);
    }

    kill_block_super(sb);

    printk(KERN_INFO "%s: filesystem unmount succesful.\n",MODNAME);
    brelse(bh);
    return;
}

//called on file system mounting 
struct dentry *msgfs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {

    struct dentry *ret;
    //just checking if the file system is already mounted- as we said, we assume for simplicity it
    //can be mounted only once
    if(unlikely(!__sync_bool_compare_and_swap(&mounted, false, true))){
        printk("%s: Device already mounted: shutting down\n", MODNAME);
        return ERR_PTR(-EBUSY);
    }
    ret = mount_bdev(fs_type, flags, dev_name, data, msgfs_fill_super);

    if (unlikely(IS_ERR(ret)))
        printk("%s: error mounting msgfilefs\n",MODNAME);
    else
        printk("%s: msgfilefs is succesfully mounted on from device %s\n",MODNAME,dev_name);

    return ret;
}

//file system structure
static struct file_system_type msgfilefs_type = {
	.owner = THIS_MODULE,
    .name           = "msgfilefs",
    .mount          = msgfs_mount,              //mount
    .kill_sb        = msgfs_kill_superblock,    //unmount
};




static int msgfilefs_init(void) {

    int ret;
    mounted = false;
    
        
    if(unlikely(hack_syscall_table()!=0)){
        printk(KERN_INFO "%s: Error in syscall table hacking", MODNAME);
        return -1;
    }

    //register filesystem
    ret = register_filesystem(&msgfilefs_type);
    if (likely(ret == 0)){
        printk(KERN_INFO "%s: sucessfully registered msgfilefs\n",MODNAME);
        
    }
    else{
        printk(KERN_INFO "%s: failed to register msgfilefs - error %d", MODNAME,ret);
    }
    
    return 0;
}

static void msgfilefs_exit(void) {

    int ret;
    unhack_syscall_table();
    
    //unregister filesystem
    ret = unregister_filesystem(&msgfilefs_type);

    if (likely(ret == 0))
        printk(KERN_INFO "%s: sucessfully unregistered file system driver\n",MODNAME);
    else
        printk(KERN_INFO "%s: failed to unregister singlefilefs driver - error %d", MODNAME, ret);
        
}

module_init(msgfilefs_init);
module_exit(msgfilefs_exit);




MODULE_LICENSE("GPL");
MODULE_AUTHOR("Adrian Baba");
MODULE_DESCRIPTION("USER-MESSAGE-FS");
