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
    
    //s_priv_info *pi=(s_priv_info *)filp->private_data;
    unsigned long current_ts = (*(unsigned long *)filp->private_data);
    bool found = false;

    char *out;  
    char *tmp;
    current_ts = (*(unsigned long *)filp->private_data);
    sb = filp->f_path.dentry->d_inode->i_sb;
    pi = (struct priv_info *)sb->s_fs_info; 
    file_size = (pi->file_size + 2) * DEFAULT_BLOCK_SIZE;

    //TESTING(printk("NOTE---> offset passed is %lld AND in file struct there's %lld", *off, filp->f_pos));
    //TESTING(printk("%s: read operation called with len %ld - and offset %lld (the current file size is %lld)\n",MODNAME, len, *off, file_size));
    blen = len;
    //this operation is not synchronized 
    //*off can be changed concurrently 
    //add synchronization if you need it for any reason

    //check that *off is within boundaries
    if (*off >= file_size){
        return 0;
    }

    //determine the block level offset for the operation
    //offset = *off % DEFAULT_BLOCK_SIZE; 

    //compute the actual index of the the first block to be read from device (ordered by writing timestamp)
    block_to_read = (*off / DEFAULT_BLOCK_SIZE);

    //needs to be here
    out = (char *)kzalloc(len+1 * sizeof(char), GFP_KERNEL);
    if(!out){
        //TESTING(printk("Error allocating memory for output buffer in VFS read!\n"));
        return -EIO;
    }
    blen = len;
    rcu_read_lock();

    
    //TESTING(printk("Just to remember, need to access block at offset %d\n", block_to_read));
    //TESTING(printk("Using %d for %d", pi->inv_bitmask[0], block_to_read -2));
    res = getBit(pi->inv_bitmask, block_to_read -2);
    if(res == ERROR){
        //TESTING(printk("Error with the requested position"));
        return 0;
    }
    else if(res == 1){
        //TESTING(printk("block to be read is invalid: searching \n"));
        get_info(actual_block->bm.tstamp_last >= current_ts);
        *off = (actual_block->bm.offset * DEFAULT_BLOCK_SIZE)+ sizeof(block_metadata);
    }
    else{
        //TESTING(printk("block to read is valid: searching for it\n"));
        get_info(actual_block->bm.offset == block_to_read);
    }

    if(!found){
        rcu_read_unlock();
        //TESTING(printk("Couldn't find the needed block!\n"));
        return 0;
    }
    //TESTING(printk("Got block at offset %d with tstamp %ld", actual_block->bm.offset, actual_block->bm.tstamp_last ));
    //while(blen > 0 && !finished){
    //to be checked
    for( int i=0; i<MAXBLOCKS; i++ ){    
        current_pos = (*off - sizeof(block_metadata)) % DEFAULT_BLOCK_SIZE;
        //TESTING(printk("Remember: current offseti is at %lld, it means block starts at %lld; current pos in char array is %lld\n", *off,*off - sizeof(block_metadata), current_pos )); 
        bh = (struct buffer_head *)sb_bread(sb, actual_block->bm.offset);
        if(!bh){
            rcu_read_unlock();
            //TESTING(printk("Error allocating memory for bh in VFS read!\n"));
            return -EIO;
        }
        //TESTING(printk("blen aka missing message is size %d", blen));
        current_db=(data_block *)bh->b_data;
        tmp = &(current_db->usrdata[current_pos]);
        //TESTING(printk("The string left in the current message should be that: %s\n", &(current_db->usrdata[current_pos])));
        //last read, we made it this far
        if((actual_block->bm.msg_size - current_pos ) > blen -1){
            //update offset properly
            
            rcu_read_unlock();
            strncat(out, tmp, blen-1);
            strncat(out, "\n", 1);
            //
            //TESTING(printk("We read all we had to read; current output will be %s\n", out));
            ret = copy_to_user(buf, out, len);
            *off += blen-1;
            //TESTING(printk("Finished: with new offset next red will be at %lld\n", (*off - sizeof(block_metadata)) % DEFAULT_BLOCK_SIZE));
            *(unsigned long *)(filp->private_data) = actual_block->bm.tstamp_last;
            //TESTING(printk("Just updated offset to %lld and will return %ld with len:%ld and bytes missed: %d\n", *off, (len-ret), len, ret));
            return (len - ret);
        }
        else{
            //TESTING(printk("%d bytes left, but the left message is %lld long: copyng those bytes and adding the slash n.\n", blen, (actual_block->bm.msg_size - current_pos) ));
            strncat(out, tmp, (actual_block->bm.msg_size - current_pos));
            strncat(out, "\n", 1);
            size += (actual_block->bm.msg_size - current_pos) +1;
            blen -= (actual_block->bm.msg_size - current_pos) +1;
            //TESTING(printk("Since we are still in the cicle, we track the fact we copied only %d so far", size));
            actual_block = list_next_or_null_rcu(&(pi->bm_list), &(actual_block->bm_list), struct block_order_node, bm_list);
            //we reach the end, can't read len bytes
            if(!actual_block){

                //update *offset properly
                rcu_read_unlock();
                *off = file_size + 1;               
                ret = copy_to_user(buf, out, size);
                //TESTING(printk("We reached the end of the file without reading all the requested data, but no problem, updating offset outside of boundaries\n"));
                //TESTING(printk("New, out of bundaries offset is %lld; copied %d bytes and the buffer is %s", *off, size, buf));
                //TESTING(printk("Returning size - ret, that means %d - %d", size, ret));
                  
                return ( size - ret );
            }
            //we didn't finish with the read but we have a next block to read
            else{
                //TESTING(printk("Need at least a new block wich we have: updating metadata properly\n"));
                *off = ((actual_block->bm.offset * DEFAULT_BLOCK_SIZE) + sizeof(block_metadata));
                *(unsigned long *)(filp->private_data) = actual_block->bm.tstamp_last;

            }
            //ret = copy_to_user(buf, out, len);
        }

    }
    //TESTING(printk("There is a sever bug if this point has been reached\n"));
    
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
        //TESTING(printk("Error allocating memory"));
        return -1;
    }
    file->private_data = (void *)tstamp;
    
    //TESTING(printk("Testing initially\n"));
    first = list_first_or_null_rcu(&(pi->bm_list), struct block_order_node, bm_list);

    if(first){
        //TESTING(printk("got one with offset %d\n", first->bm.offset));
        *tstamp = first->bm.tstamp_last;
        file->f_pos=(((first->bm.offset) * DEFAULT_BLOCK_SIZE) + sizeof(block_metadata));
        //TESTING(printk("offset1 is %lld\n", file->f_pos));
    }
    else{
        ////TESTING(printk("Got void, no offset\n" ));
        *tstamp = 0;
        file->f_pos=file_size + 1;
        //TESTING(printk("offset2 is %lld\n", file->f_pos));
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

   //TESTING(printk("%s: running the lookup inode-function for name %s",MODNAME,child_dentry->d_name.name));

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
    the_inode->i_fop = &msgfilefs_dir_operations;
	the_inode->i_op = &msgfilefs_inode_ops;

	//just one link for this file
	set_nlink(the_inode,1);

	//now we retrieve the file size via the FS specific inode, putting it into the generic inode
    	bh = (struct buffer_head *)sb_bread(sb, MSGFS_INODES_BLOCK_NUMBER);
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
