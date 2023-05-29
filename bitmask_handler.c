/*
 *  Just a stupid handler for the kernel-side kept bitmask, needed it to set a particular 
 *  bit in the bitmask to 1 or 0, to initialize the bitmask with all 0s, to get what's in 
 *  the bitmask in a particular position or to get the first invalid block
 */

#include "msgfilefs_kernel.h"

static DEFINE_MUTEX(bm_mutex);

#define INT_BIT_SIZE (sizeof(int) * 8)

//set bit in position pos. true as parameter for 1 (invalidate), false for 0 (validate)
int setBit(unsigned int bitmask[], int pos, bool value){

    int index = pos / INT_BIT_SIZE;
    int num = pos % INT_BIT_SIZE;
    unsigned int res;
    if(pos > datablocks || pos<0){
        return ERROR;
    }

    if(value ==true){
        res = bitmask[index] | (1u << (INT_BIT_SIZE - num - 1));
        __atomic_exchange_n(&(bitmask[index]), res, __ATOMIC_SEQ_CST);
        
    }
    else{
        res = bitmask[index] & ~(1u << (INT_BIT_SIZE - num - 1) );
        __atomic_exchange_n(&(bitmask[index]), res, __ATOMIC_SEQ_CST);
    }
    return 1;

}

//just init with all 0s
void initBit(unsigned int bitmask[]){

    int i;
    for(i=0; i< MASK_SIZE; i++){
        bitmask[i] = 0;
    }
    
}

//get bit in position "pos"
int getBit(unsigned int bitmask[], int pos){
    int index = pos / (sizeof(int) * 8);
    int num = INT_BIT_SIZE -1 - (pos % INT_BIT_SIZE);
    unsigned int res;
    if(pos > datablocks || pos < 0){
        return ERROR;
    }
    mutex_lock(&bm_mutex);
    res = (bitmask[index] & (1u << num));
    mutex_unlock(&bm_mutex);
    if(res == 0) return 0;
    return 1;

}

//gets first invalid block and makes it valid returning his offset in the bitmask
int getInvBit(unsigned int bitmask[]){

    int i, position, target = -1;
    unsigned int cpy;
    unsigned int mask = 1 << (INT_BIT_SIZE -1);

    mutex_lock(&bm_mutex);
    for (i = 0; i < MASK_SIZE; i++) {
        if (bitmask[i] != 0) {
            position = 0;
            cpy = bitmask[i];
            while (((cpy & mask) == 0) && (cpy != 0)) {
                cpy <<= 1;
                position++;
            }
            if(!cpy){
                return ERROR;
            }
            target = ((i * INT_BIT_SIZE) + position);
            if(setBit(bitmask, target, false)){
                mutex_unlock(&bm_mutex);
                return target;
            };
            mutex_unlock(&bm_mutex);
            return ERROR;
        }
    }
    mutex_unlock(&bm_mutex);
    return ERROR;

}







