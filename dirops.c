/*
 * This file only implements the only directory operation we need, the iteration 
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

#include "msgfilefs_kernel.h"

//this iterate function just returns 3 entries: . and .. and then the name of the unique file of the file system
static int msgfilefs_iterate(struct file *file, struct dir_context* ctx) {

    
	if(ctx->pos >= (2 + 1)) return 0;//we cannot return more than . and .. and the unique file entry

	if (ctx->pos == 0){

		if(!dir_emit_dot(file, ctx)){
			return 0;
		}
		else{
			ctx->pos++;
		}
	
	}

	if (ctx->pos == 1){
  
		if(!dir_emit_dotdot(file, ctx)){
			return 0;
		}
		else{
			ctx->pos++;
		}
	
	}
	if (ctx->pos == 2){
		
   		if(!dir_emit(ctx, MSG_FILE_NAME, FILENAME_LEN, MSGFS_FILE_INODE_NUMBER, DT_UNKNOWN)){
			return 0;
		}
		else{
			ctx->pos++;
		}
	
	}
	return 0;

}

const struct file_operations msgfilefs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate = msgfilefs_iterate,
};
