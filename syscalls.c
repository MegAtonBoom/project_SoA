/*
 * System calls definitions
 */

#include "msgfilefs_kernel.h" 
#include <linux/syscalls.h>
#include <linux/version.h>
#include <linux/buffer_head.h>
#include "lib/include/scth.h"
#include <linux/rculist.h>


//TO BE FIXED: FILE SIZE!  IF NEEDED WITH READ
//TO BE THINKED: mutexes ONLY for the rcu write? 



#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(2, _put_data, char *, source, size_t, size){
#else
asmlinkage int sys_put_data(char * source, size_t size){
#endif

    int offset, real_offset;
    struct buffer_head *bh ;
    struct timespec64 time;
    struct block_order_node *node;
    struct priv_info *pi = (struct priv_info *)the_sb->s_fs_info;
    data_block *db;
    char *msg;

    if(!mounted){
        //TESTING(printk("Device not mounted, returning error"));
        return -ENODEV;
    }
    if(size > MSGBUF_SIZE){
        //TESTING(printk("Given size too big, the message can't be written in any block\n"));
        return -ENOMEM;
    }
    node = (struct block_order_node *)kmalloc(sizeof(struct block_order_node), GFP_KERNEL);
    //TESTING(printk("first places in inv_mas: %u\n", pi->inv_bitmask[0]));
     //here?
    if((offset=getInvBit(pi->inv_bitmask))==ERROR){
        //TESTING(printk("No free block avaiable\n"));
        mutex_unlock(&pi->write_mt);
        kfree(node);
        return -ENOMEM;
    }
    msg = (char *)kzalloc(size, GFP_KERNEL);
    if(!msg){
        //TESTING(printk("Error allocating buffer"));
        return -ENOMEM;
    }

    if(copy_from_user(msg, source, size)){
        mutex_unlock(&(pi->write_mt));
        setBit(pi->inv_bitmask, offset, true);
        brelse(bh);
        //TESTING(printk("Couldn't copy the whole message! \n"));
        kfree(msg);
        return -ENOMEM;
    };

   //TESTING(printk("Offset got in mask: %d, real: %d\n", offset, offset+2));
    real_offset = offset + 2;
    bh = sb_bread(the_sb, real_offset);
    db = (data_block *)bh->b_data;
    //here
    mutex_lock(&(pi->write_mt));

    //the_sb->s_fs_info->file_size += DEFAULT_BLOCK_SIZE;
    ktime_get_real_ts64(&time);
    db->bm.offset = real_offset;
    db->bm.invalid = false;
    db->bm.msg_size = size;
    db->bm.tstamp_last = (unsigned int)time.tv_sec;
    //TESTING(printk("NOTE---->Got timestamp %ld for current message\n", db->bm.tstamp_last));
    //or maybe here?????
    strncat(db->usrdata ,msg, size);
    db->usrdata[size+1]='\0';
    node->bm = db->bm;
    //TESTING(printk("timestamp now: %ld", db->bm.tstamp_last));
    list_add_tail_rcu(&(node->bm_list), &(pi->bm_list));

    mutex_unlock(&pi->write_mt);

    mark_buffer_dirty(bh);
    #ifdef SYNCHRONIZE
        sync_dirty_buffer(bh);
    #endif
    //needed brelse?
    //the_sb->s_fs_info->file_size += (sizeof(block_metadata) + size); 
    //TESTING(printk("Data put at offset %d\n", offset));
    kfree(msg);
    return offset;

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(3, _get_data, int, offset, char *, destination, size_t, size){
#else
asmlinkage int sys_get_data(int offset, char * destination, size_t size){
#endif

    struct buffer_head *bh;
    struct priv_info *pi = (struct priv_info *)the_sb->s_fs_info;
    data_block *db;
    int m_size=0, real_offset;           //missed bytes in the copy

    if(!mounted){
        //TESTING(printk("Device not mounted, returning error"));
        return -ENODEV;
    }

    if(offset<0 || offset > datablocks){
        //TESTING(printk("Invalid offset\n"));
        return -EIO;
    }

    if((getBit(pi->inv_bitmask, offset))==ERROR){
        //TESTING(printk("Invalid block at given offset\n"));
        return -ENODATA;
    }
    real_offset = offset + 2;
    mutex_lock(&pi->write_mt);
    bh = sb_bread(the_sb, real_offset);
    if(!bh){
        mutex_unlock(&pi->write_mt);
        //TESTING(printk("Error ih bread in get_data\n"));
	    return -EIO;
    }

    db = (data_block *)bh->b_data;
    brelse(bh);
    if(db->bm.msg_size == 0){
        mutex_unlock(&pi->write_mt);
        //TESTING(printk("Requested block is empty\n"));
        return 0;
    }
    else if(db->bm.msg_size < size){
        m_size = copy_to_user(destination, db->usrdata, db->bm.msg_size);
        mutex_unlock(&pi->write_mt);
        //TESTING(printk("Copied from %lu requested to %d actual\n",size, db->bm.msg_size));
        return (db->bm.msg_size - m_size);
    }
    m_size = copy_to_user(destination, db->usrdata, size);
    mutex_unlock(&pi->write_mt);
    //TESTING(printk("Copied all %lu bytes\n",size));
    return (size - m_size);

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset){
#else
asmlinkage int sys_invalidate_data(int offset){
#endif

    struct block_order_node *curr; //*new;
    struct priv_info *pi = the_sb->s_fs_info;
    struct buffer_head *bh;
    data_block *db;
    bool changed = false;
    int real_offset;
   //TESTING(printk("INVALIDATE---------"));
    if(!mounted){
        //TESTING(printk("Device not mounted, returning error"));
        return -ENODEV;
    }

    if(offset<0 || offset > datablocks){
        //TESTING(printk("Invalid offset\n"));
        return -EIO;
    }

    //mutex_lock(&the_sb->s_fs_info->write_mt);  //NEEDED? I THINK SO
    real_offset = offset + 2;
    rcu_read_lock();

    list_for_each_entry_rcu(curr, (&pi->bm_list), bm_list){
        if(curr->bm.offset == real_offset){
            /**new = *curr;
            new->bm.invalid = true;*/
            changed = true;
            mutex_lock(&pi->write_mt);
            //the_sb->s_fs_info->file_size -= DEFAULT_BLOCK_SIZE;
           //TESTING(printk("Deleting...\n"));
            list_del_rcu(&curr->bm_list);
            mutex_unlock(&pi->write_mt);
            break;
        }
    }
    rcu_read_unlock();
    if(!changed){
        
        
        //TESTING(printk("The block at the given offset is not in use\n"));
        return -ENODATA;
    }
    
    synchronize_rcu();
    kfree(curr);

    //Bit changing has to be last
    bh = sb_bread(the_sb, real_offset);
    if(!bh){
        //mutex_unlock(&pi->write_mt);
        //TESTING(printk("Error allocating memory\n"));
	    return -EIO;
    }
    db = (data_block *)bh->b_data;
    INITIALIZE(db);
    mark_buffer_dirty(bh);
    #ifdef SYNCHRONIZE
        sync_dirty_buffer(bh);
    #endif
    //mutex_unlock(&the_sb->s_fs_info->write_mt);
    //TESTING(printk("Correctly written block in device at offset %d\n", offset));
    //TESTING(printk("INVALIDATE BEFORE MASK: %u\n", pi->inv_bitmask[0]));
    if(setBit(pi->inv_bitmask, offset, true)){
       //TESTING(printk("Correctly invalidated block in bitmask\n"));
    }
    else{
        //mutex_unlock(&pi->write_mt);
       //TESTING(printk("Could not clear the bitmask\n"));
    }
    //TESTING(printk("INVALIDATE AFTER MASK: %u\n", pi->inv_bitmask[0]));
    return 0;
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_put_data = (unsigned long)__x64_sys_put_data;
long sys_get_data = (unsigned long)__x64_sys_get_data;
long sys_invalidate_data = (unsigned long)__x64_sys_invalidate_data;
#endif

int hack_syscall_table(){
    int ret=0;

    new_sys_call_array[0] = (unsigned long)sys_put_data;
    new_sys_call_array[1] = (unsigned long)sys_get_data;
    new_sys_call_array[2] = (unsigned long)sys_invalidate_data;
    
    ret = get_entries(restore,HACKED_ENTRIES,(unsigned long*)the_syscall_table,&the_ni_syscall);

    if (ret != HACKED_ENTRIES){
            //TESTING(printk("%s: could not hack %d entries (just %d)\n",MODNAME,HACKED_ENTRIES,ret));
            return -1;
    }

    unprotect_memory();
    for(int i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = (unsigned long)new_sys_call_array[i];
    }
    protect_memory();
    //TESTING(printk("syscalltable correctly hacked\n"));
    return 0;
}

void unhack_syscall_table(){

    int i;
    //TESTING(printk("%s: shutting down\n",MODNAME));

    unprotect_memory();
    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = the_ni_syscall;
    }
    protect_memory();
    //TESTING(printk("%s: sys-call table restored to its original content\n",MODNAME));
    
}
