/*
 * This file implements the requested file operations needet to manage working sessions on
 * our unique file considering the requested feature: the read han't to be block ordered but
 * time ordered based on write timestamp.
 *
 * 1-   Open: opening in our case needs some initialization before read: we need to know 
 *            who is the first block whe need to read- since we want a time ordered read, it 
 *            could be any of the MAXBLOCKS-2 datablocks currently valid (we can get those
 *            informations form the superblock private data)- to get his timestamp in case it gets
 *            invalidated meanwhile and the actual offset in the device
 *
 * 2-   Release: it just has to free the kernel space used by our private file descriptor metadata
 *            to store the last timestamp read
 *
 * 3-   Read: The read operation allows us to read from the file in a time ordered manner.
 *            A single call to the read function can risult in multiple blocks actually read
 *            I.E. the next two blocks to be read have each 10 bytes of data and we ask for 15 bytes:
 *            we will get the full content of the first block and half of the second block.
 *            Using RCU mechanisms and syncronization, we can't have someone overwriting the device 
 *            (file) content while a single read call has to access to more than one block.
 *            
 *            N.B.1: The actual buffer the read will return will contain a \n for each piece of
 *            block read. That means that, in the previous example, it will return the first 10 bytes
 *            of the first block, then a \n, then the first 3 bytes of the second block and then 
 *            another \n.
 *
 *            N.B.2: A second read call on the same session, instead, if the current block has been
 *            invalidated in the meanwhile, will start the reading from the next valid block in 
 *            timestamp order. That means that, in the previous example, if we call "A" the first
 *            block with 10 bytes and we assume it has timestamp 1 and we call "B" the second block
 *            from which we read only 3 bytes and we assume it has timestamp 2 and we call "C" the 
 *            valid block with timestamp 3 that's right after the block "B", then if after the read
 *            we called in the previous example the block B gets invalidated and after that we call 
 *            another read requesting 8 bytes, we won't get the remaining bytes in "B" but the  
 *            first bytes of the "C" block.
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

#include "msgfilefs_kernel.h"


ssize_t msgfilefs_read(struct file * filp, char __user * buf, size_t len, loff_t * off) {
    
    struct buffer_head *bh = NULL;
    uint64_t file_size;
    int ret, blen, size=0;
    loff_t current_pos; 
    struct super_block *sb;
    struct priv_info *pi;

    struct block_order_node *actual_block;

    data_block *current_db;
    
    unsigned long current_ts;
    bool found = false;

    char *out;  
    char *tmp;
    
    //we get the timestamp of the last read block
    current_ts = (*(unsigned long *)filp->private_data);

    sb = filp->f_path.dentry->d_inode->i_sb;
    pi = (struct priv_info *)sb->s_fs_info; 
    file_size = (pi->file_size + 2) * DEFAULT_BLOCK_SIZE;

    //blen = tmp container of the remaining bytes to read
    blen = len;

    //check that *off is within boundaries
    if (*off >= file_size){
        return 0;
    }

    //check on len, we return 0 with len 1 since we would only put a \n
    if(len<1){
        return 0;
    }

    out = (char *)kzalloc(len+1 * sizeof(char), GFP_KERNEL);
    if(!out){  
        return -EIO;
    }

    rcu_read_lock();

    //we need the last block we read, ot he next in chronological order
    list_for_each_entry_rcu(actual_block, (&pi->bm_list), bm_list){
        if (actual_block->bm.tstamp_last >= current_ts){
            found = true;
            break;
        }
    }
    //just leave the rcu read block and quit returning 0, if we can't find that block
    if(!found){
        rcu_read_unlock();
        kfree(out);
        return 0;
    }

    /* we update the offset only if the timestamp of the last read block is not the same as 
    *  the one of the block we are currently reading (that means that the last block has beeen
    *  invalidated meanwhile, so the curren one might have a different offset in the file structure)
    */
    if(actual_block->bm.tstamp_last > current_ts){
        *off = (actual_block->bm.offset * DEFAULT_BLOCK_SIZE)+ sizeof(block_metadata);
    }
    
    //we look block after block to concat the requested msg size
    for( int i=0; i<MAXBLOCKS; i++ ){ 

        //current offset inside the msg buffer inside the block, according to the current offset   
        current_pos = (*off - sizeof(block_metadata)) % DEFAULT_BLOCK_SIZE;
        bh = (struct buffer_head *)sb_bread(sb, actual_block->bm.offset);
        if(!bh){
            rcu_read_unlock();
            kfree(out);
            return -EIO;
        }
        current_db=(data_block *)bh->b_data;
        tmp = &(current_db->usrdata[current_pos]);
        //We made to the last block we need to read
        if((actual_block->bm.msg_size - current_pos ) > blen -1){
            
            
            rcu_read_unlock();
            strncat(out, tmp, blen-1);
            strncat(out, "\n", 1);
            ret = copy_to_user(buf, out, len);
            *off += blen-1;
            *(unsigned long *)(filp->private_data) = actual_block->bm.tstamp_last;
            kfree(out);
            return (len - ret);
        }
        //the current block is not enough to fill the buffer
        else{
            strncat(out, tmp, (actual_block->bm.msg_size - current_pos));
            strncat(out, "\n", 1);
            size += (actual_block->bm.msg_size - current_pos) +1;
            blen -= (actual_block->bm.msg_size - current_pos) +1;
            actual_block = list_next_or_null_rcu(&(pi->bm_list), &(actual_block->bm_list), struct block_order_node, bm_list);
            
            //We reached the last block and we can't fill entirely the buffer, so we return what we
            //have read so far
            if(!actual_block){

                //update *offset properly
                rcu_read_unlock();
                *off = file_size + 1;               
                ret = copy_to_user(buf, out, size);
                kfree(out);
                return ( size - ret );
            }
            //we didn't fill the buffer but we can read the next ordered block
            else{
                *off = ((actual_block->bm.offset * DEFAULT_BLOCK_SIZE) + sizeof(block_metadata));
                *(unsigned long *)(filp->private_data) = actual_block->bm.tstamp_last;

            }
            
        }

    }
    //We shouldn't reach this point!
    printk("%s: There was a bug in the read, returning 0\n", MODNAME);
    kfree(out);
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
    //in case we have something in the list, since it's already ordered we can use its 
    //timestamp and offset
    if(first){
        *tstamp = first->bm.tstamp_last;
        file->f_pos=(((first->bm.offset) * DEFAULT_BLOCK_SIZE) + sizeof(block_metadata));
        
    }
    //if the list of valid entries is empty, there's nothing to read
    else{
        
        *tstamp = 0;
        file->f_pos=file_size + 1;
        
    }
   return 0;
}

//release: only freeing the kernel space reserved for the file descriptor metadata
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
        #if LINUX_VERSION_CODE >= KERNEL_VERSION(5,16,0)
            inode_init_owner(&init_user_ns, the_inode, NULL, S_IFREG);
        #else
            inode_init_owner(the_inode, NULL, S_IFREG);
        #endif

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

const struct inode_operations msgfilefs_inode_ops = {
    .lookup = msgfilefs_lookup,
};

const struct file_operations msgfilefs_file_operations = {
    .owner = THIS_MODULE,
    .read = msgfilefs_read,
    .open = msgfilesfs_open,
    .release = msgfilefs_release
};
