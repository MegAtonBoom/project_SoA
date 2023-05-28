#include "msgfilefs_kernel.h"

static DEFINE_MUTEX(bm_mutex);

//set bit in position pos. true as value for 1 (invalid), false for 0
int setBit(unsigned int bitmask[], int pos, bool value){

    int index = pos / (sizeof(int) * 8);
    int num = pos % (sizeof(int) * 8);
    unsigned int res;
    if(pos > datablocks){
        return ERROR;
    }

    if(value ==true){
        res = bitmask[index] | (1u << ((sizeof(int)*8) - num - 1));
        __atomic_exchange_n(&(bitmask[index]), res, __ATOMIC_SEQ_CST);
        
    }
    else{
        res = bitmask[index] & ~(1u << ((sizeof(int)*8) - num - 1) );
        __atomic_exchange_n(&(bitmask[index]), res, __ATOMIC_SEQ_CST);
    }
    return 1;

}

void initBit(unsigned int bitmask[]){

    int i;
    for(i=0; i< MASK_SIZE; i++){
        bitmask[i] = 0;
    }
    
}

int getBit(unsigned int bitmask[], int pos){
    int index = pos / (sizeof(int) * 8);
    int num = sizeof(int) * 8 -1 - (pos % (sizeof(int) * 8));
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

int getInvBit(unsigned int bitmask[]){

    int i, position, target = -1;
    unsigned int cpy;
    unsigned int mask = 1 << ((sizeof(unsigned int) *8) -1);

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
            target = ((i * sizeof(int) * 8) + position);
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







