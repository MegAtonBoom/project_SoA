#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/timekeeping.h>
#include <linux/time.h>
#include <linux/buffer_head.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "msgfilefs_kernel.h"




#define get_info(cond)\
        list_for_each_entry_rcu(actual_block, (&pi->bm_list), bm_list){\
            if (cond){\
                found = true;\
                break;\
            }\
        }

ssize_t msgfilefs_read(struct file * filp, char __user * buf, size_t len, loff_t * off) {
    
    struct buffer_head *bh = NULL;
    uint64_t file_size;
    int ret, blen, size=0;
    loff_t current_pos;
    int block_to_read, res;   //index of the block to be read from device
    struct super_block *sb;
    struct priv_info *pi;

    struct block_order_node *actual_block;

    data_block *current_db;
    
    unsigned long current_ts;
    bool found = false;

    char *out;  
    char *tmp;
    
    current_ts = (*(unsigned long *)filp->private_data);
    sb = filp->f_path.dentry->d_inode->i_sb;
    pi = (struct priv_info *)sb->s_fs_info; 
    file_size = (pi->file_size + 2) * DEFAULT_BLOCK_SIZE;

    blen = len;

    //check that *off is within boundaries
    if (*off >= file_size){
        return 0;
    }



    //compute the actual index of the the first block to be read from device (ordered by writing timestamp)
    block_to_read = (*off / DEFAULT_BLOCK_SIZE);

    
    out = (char *)kzalloc(len+1 * sizeof(char), GFP_KERNEL);
    if(!out){
        
        return -EIO;
    }

    blen = len;
    rcu_read_lock();

    
    
    res = getBit(pi->inv_bitmask, block_to_read -2);
    if(res == ERROR){
        
        return 0;
    }   
    else if(res == 1){
        
        get_info(actual_block->bm.tstamp_last >= current_ts);
        *off = (actual_block->bm.offset * DEFAULT_BLOCK_SIZE)+ sizeof(block_metadata);
    }
    else{
        
        get_info(actual_block->bm.offset == block_to_read);
    }

    if(!found){
        rcu_read_unlock();
        return 0;
    }
    
    for( int i=0; i<MAXBLOCKS; i++ ){    
        current_pos = (*off - sizeof(block_metadata)) % DEFAULT_BLOCK_SIZE;
        bh = (struct buffer_head *)sb_bread(sb, actual_block->bm.offset);
        if(!bh){
            rcu_read_unlock();
            return -EIO;
        }
        current_db=(data_block *)bh->b_data;
        tmp = &(current_db->usrdata[current_pos]);
        //last read, we made it this far
        if((actual_block->bm.msg_size - current_pos ) > blen -1){
            
            
            rcu_read_unlock();
            strncat(out, tmp, blen-1);
            strncat(out, "\n", 1);
            ret = copy_to_user(buf, out, len);
            *off += blen-1;
            *(unsigned long *)(filp->private_data) = actual_block->bm.tstamp_last;
            return (len - ret);
        }
        else{
            strncat(out, tmp, (actual_block->bm.msg_size - current_pos));
            strncat(out, "\n", 1);
            size += (actual_block->bm.msg_size - current_pos) +1;
            blen -= (actual_block->bm.msg_size - current_pos) +1;
            actual_block = list_next_or_null_rcu(&(pi->bm_list), &(actual_block->bm_list), struct block_order_node, bm_list);
            
            //we reach the end, can't read len bytes
            if(!actual_block){

                //update *offset properly
                rcu_read_unlock();
                *off = file_size + 1;               
                ret = copy_to_user(buf, out, size);
                  
                return ( size - ret );
            }

            //we didn't finish with the read but we have a next block to read
            else{
                *off = ((actual_block->bm.offset * DEFAULT_BLOCK_SIZE) + sizeof(block_metadata));
                *(unsigned long *)(filp->private_data) = actual_block->bm.tstamp_last;

            }
            
        }

    }
    
    return 0;        
}



//nothing special, only some initialization
int msgfilesfs_open(struct inode *inode, struct file *file) {
    
    struct super_block *sb = file->f_path.dentry->d_inode->i_sb;
    struct priv_info *pi= sb->s_fs_info;
    struct inode * the_inode = file->f_inode;
    uint64_t file_size = the_inode->i_size;
    unsigned long *tstamp;
    struct block_order_node *first = NULL;

    tstamp = (unsigned long *)kzalloc(sizeof(unsigned long), GFP_KERNEL);
    if(!tstamp){
        return -1;
    }
    file->private_data = (void *)tstamp;
    
    first = list_first_or_null_rcu(&(pi->bm_list), struct block_order_node, bm_list);

    if(first){
        *tstamp = first->bm.tstamp_last;
        file->f_pos=(((first->bm.offset) * DEFAULT_BLOCK_SIZE) + sizeof(block_metadata));
        
    }
    else{
        
        *tstamp = 0;
        file->f_pos=file_size + 1;
        
    }
   return 0;
}

//release: only unlocking the mutex since we can open only once the file
int msgfilefs_release(struct inode *inode, struct file *file) {

    kfree(file->private_data);
    file->private_data = NULL;

   return 0;

}

struct dentry *msgfilefs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {

    struct msgfs_inode *FS_specific_inode;
    struct super_block *sb = parent_inode->i_sb;
    struct buffer_head *bh = NULL;
    struct inode *the_inode = NULL;

    printk("%s: running the lookup inode-function for name %s",MODNAME,child_dentry->d_name.name);

    if(!strcmp(child_dentry->d_name.name, MSG_FILE_NAME)){

	
        //get a locked inode from the cache 
        the_inode = iget_locked(sb, 1);
        if (!the_inode)
            return ERR_PTR(-ENOMEM);

        //already cached inode - simply return successfully
        if(!(the_inode->i_state & I_NEW)){
            return child_dentry;
        }


        //this work is done if the inode was not already cached
        inode_init_owner(&init_user_ns, the_inode, NULL, S_IFREG );
        the_inode->i_mode = S_IFREG | S_IRUSR | S_IRGRP | S_IROTH | S_IWUSR | S_IWGRP | S_IXUSR | S_IXGRP | S_IXOTH;
        the_inode->i_fop = &msgfilefs_file_operations;
        the_inode->i_op = &msgfilefs_inode_ops;

        //just one link for this file
        set_nlink(the_inode,1);

        //now we retrieve the file size via the FS specific inode, putting it into the generic inode
        bh = (struct buffer_head *)sb_bread(sb, MSGFS_INODES_BLOCK_NUMBER );
        if(!bh){
            iput(the_inode);
            return ERR_PTR(-EIO);
        }
        FS_specific_inode = (struct msgfs_inode*)bh->b_data;
        the_inode->i_size = FS_specific_inode->file_size;
        brelse(bh);

        d_add(child_dentry, the_inode);
        dget(child_dentry);

        //unlock the inode to make it usable 
        unlock_new_inode(the_inode);

        return child_dentry;
    }

    return NULL;

}

//look up goes in the inode operations
const struct inode_operations msgfilefs_inode_ops = {
    .lookup = msgfilefs_lookup,
};

const struct file_operations msgfilefs_file_operations = {
    .owner = THIS_MODULE,
    .read = msgfilefs_read,
    .open = msgfilesfs_open,
    .release = msgfilefs_release
};
