/*
 * System calls related stuff
 *   0- hack and unhack syscall table: using the other module info, adds the new syscalls in
 *      the empty locations of the syscal table. For now we can assume that the 3 calls will
 *      be placed in position 134 - 156 - 174.
 *
 *   1- put_data system call: takes a char buffer and a size; looks for an empty block int he 
 *      device - if there's any- and puts size bytes from the buffer here. Returns the offset
 *       needed to retrieve the block content with the second syscall, or errors otherwise.
 *
 *   2- get_data system call: takes an offset, a char buffer and a size; retrieve the user data
 *      from the block. Returns the size of the copied message- exactly the size parameter if 
 *      nothing went wrong, or errors otherwise. 
 *
 *   3- invalidate_data system call: takes the offset of the block you need to invalidate.
 *      Returns 0 upon success, errors otherwise.
 *
 *      NOTE: The offset mentioned above is not the real offset in the device- it's just 
 *            real offset - 2 (just not counting superblock and file inode)
 */


#include "msgfilefs_kernel.h" 
#include <linux/syscalls.h>
#include <linux/version.h>
#include <linux/buffer_head.h>
#include "lib/include/scth.h"
#include <linux/rculist.h>

//even if the syscall is present, should return error if the device is not mounted
#define CHECK_MOUNTED\
    if(!mounted){\
        return -ENODEV;\
    }

//checking offset inside boundaries of the file
#define CHECK_OFFSET\
    if(offset<0 || offset > datablocks){\
        return -EIO;\
    }



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

    CHECK_MOUNTED;

    //can't insert in a block more than MSGBUF_SIZE-1 (taking a byte for the \0) bytes 
    if(size > MSGBUF_SIZE - 1){
        return -ENOMEM;
    }

    node = (struct block_order_node *)kmalloc(sizeof(struct block_order_node), GFP_KERNEL);
    if((offset=getInvBit(pi->inv_bitmask))==ERROR){
        kfree(node);
        return -ENOMEM;
    }

    msg = (char *)kzalloc(size, GFP_KERNEL);
    if(!msg){
        return -ENOMEM;
    }

    //The message has to be copied all in once: if we can't, we chand everything back and return error
    if(copy_from_user(msg, source, size)){
        setBit(pi->inv_bitmask, offset, true);
        brelse(bh);
        kfree(msg);
        return -ENOMEM;
    };

    real_offset = offset + 2;

    bh = sb_bread(the_sb, real_offset);
    db = (data_block *)bh->b_data;

    mutex_lock(&(pi->write_mt));

    ktime_get_real_ts64(&time);
    db->bm.offset = real_offset;
    db->bm.invalid = false;
    db->bm.msg_size = size;
    db->bm.tstamp_last = (unsigned int)time.tv_sec;
    strncat(db->usrdata ,msg, size);
    db->usrdata[size+1]='\0';
    node->bm = db->bm;
    list_add_tail_rcu(&(node->bm_list), &(pi->bm_list));

    mutex_unlock(&pi->write_mt);

    mark_buffer_dirty(bh);
    #ifdef SYNCHRONIZE
        sync_dirty_buffer(bh);
    #endif

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

    CHECK_MOUNTED;

    CHECK_OFFSET;

    //if the block is currently invalid, we deny the request
    if((getBit(pi->inv_bitmask, offset))==ERROR){
        return -ENODATA;
    }

    real_offset = offset + 2;
    mutex_lock(&pi->write_mt);

    bh = sb_bread(the_sb, real_offset);
    if(!bh){
        mutex_unlock(&pi->write_mt);

        return -EIO;
    }

    db = (data_block *)bh->b_data;
    brelse(bh);

    //if the message size is 0 we simply return 0
    if(db->bm.msg_size == 0){
        mutex_unlock(&pi->write_mt);
        return 0;
    }

    //if the massage is shorter than the user requested size, we copy message len bytes
    else if(db->bm.msg_size < size){
        m_size = copy_to_user(destination, db->usrdata, db->bm.msg_size);
        mutex_unlock(&pi->write_mt);
        return (db->bm.msg_size - m_size);
    }

    //otherwise we copy only the requested size 
    m_size = copy_to_user(destination, db->usrdata, size);
    mutex_unlock(&pi->write_mt);

    return (size - m_size);

}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _invalidate_data, int, offset){
#else
asmlinkage int sys_invalidate_data(int offset){
#endif

    struct block_order_node *curr; 
    struct priv_info *pi = the_sb->s_fs_info;
    struct buffer_head *bh;
    data_block *db;
    bool changed = false;
    int real_offset;

    CHECK_MOUNTED;
    CHECK_OFFSET;

    real_offset = offset + 2;
    rcu_read_lock();

    //looking for the requested block in the sb private list of current valid blocks
    list_for_each_entry_rcu(curr, (&pi->bm_list), bm_list){
        if(curr->bm.offset == real_offset){
            changed = true;
            mutex_lock(&pi->write_mt);
            list_del_rcu(&curr->bm_list);
            mutex_unlock(&pi->write_mt);
            break;
        }
    }
    rcu_read_unlock();

    //return ENODATA if we couldn't find the requested block
    if(!changed){ 
        return -ENODATA;
    }
    
    //needed for the read VFS file operation
    synchronize_rcu();
    kfree(curr);

    
    bh = sb_bread(the_sb, real_offset);
    if(!bh){
        return -EIO;
    }
    db = (data_block *)bh->b_data;

    INITIALIZE(db);
    mark_buffer_dirty(bh);
    #ifdef SYNCHRONIZE
        sync_dirty_buffer(bh);
    #endif

    //Bit changing has to be last
    setBit(pi->inv_bitmask, offset, true);
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
            return -1;
    }

    unprotect_memory();
    for(int i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = (unsigned long)new_sys_call_array[i];
    }
    protect_memory();
    return 0;
}

void unhack_syscall_table(){

    int i;
    
    unprotect_memory();
    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = the_ni_syscall;
    }
    protect_memory();
    
}
