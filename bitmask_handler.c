#include "msgfilefs_kernel.h"

static DEFINE_MUTEX(bm_mutex);

//set bit in position pos. true as value for 1 (invalid), false for 0
int setBit(unsigned int bitmask[], int pos, bool value){

    int index = pos / (sizeof(int) * 8);
    int num = pos % (sizeof(int) * 8);
    unsigned int res;
    //TESTING(printk("Old bitmask value is: %u, index actually modified is %d at pos %d", bitmask[0], index, num));
    if(pos > datablocks){
        //TESTING(printk("position requested outside boundaries for setBit- at pos %d\n", pos));
        return ERROR;
    }

    if(value ==true){
        //TESTING(printk("Got the value true"));
        res = bitmask[index] | (1u << ((sizeof(int)*8) - num - 1));
        __atomic_exchange_n(&(bitmask[index]), res, __ATOMIC_SEQ_CST);
        //bitmask[index] |= (1u << ((sizeof(int)*8) - num - 1) );
    }
    else{
        //TESTING(printk("Got the value false"));
        res = bitmask[index] & ~(1u << ((sizeof(int)*8) - num - 1) );
        //bitmask[index] &= ~(0u << ((sizeof(int)*8) - num - 1) );
        __atomic_exchange_n(&(bitmask[index]), res, __ATOMIC_SEQ_CST);
    }
    //TESTING(printk("Bit correctly stted, new value: %u\n", bitmask[index]));
    return 1;

}

//useless? tbd
void initBit(unsigned int bitmask[]){

    int i;
    for(i=0; i< MASK_SIZE; i++){
        bitmask[i] = 0;
    }
    //TESTING(printk("bitmask correctly initialized\n"));

}

int getBit(unsigned int bitmask[], int pos){
    int index = pos / (sizeof(int) * 8);
    int num = sizeof(int) * 8 -1 - (pos % (sizeof(int) * 8));
    unsigned int res;
    //TESTING(printk("Checking at pos %d, so, index %d AND << of %d",pos,index,num));
    if(pos > datablocks || pos < 0){
        //TESTING(printk("position requested outside boundaries for getBit- at %d\n", pos));
        return ERROR;
    }
    mutex_lock(&bm_mutex);
    res = (bitmask[index] & (1u << num));
    //TESTING(printk("bitmask is %u and mask is %u, ris is %d", bitmask[0], 1u << num, res));
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
        //TESTING(printk("checking %u\n", bitmask[i]));
        //TESTING(printk("the used mask is %u\n", mask));
        if (bitmask[i] != 0) {
            position = 0;
            cpy = bitmask[i];
            //TESTING(printk("cpy value: %u", cpy));
            while (((cpy & mask) == 0) && (cpy != 0)) {
                //TESTING(printk("Shifted\n"));
                cpy <<= 1;
                //TESTING(printk("cpy value: %u", cpy));
                position++;
            }
            if(!cpy){
                //TESTING(printk("Couldn't take a valid entry for a bug!\n"));
                return ERROR;
            }
            //target = ((i * sizeof(int)) + (sizeof(int) - position));
            target = ((i * sizeof(int) * 8) + position);
            //TESTING(printk("Got %d at i: %d, so setting target as %d\n", position, i, target));
            if(setBit(bitmask, target, false)){
                mutex_unlock(&bm_mutex);
                //TESTING(printk("Correctly got the first invalid entry at pos %d and setted bit\n", target));
                return target;
            };
            mutex_unlock(&bm_mutex);
            //TESTING(printk("Couldn't take the valid entry! This point shouldn't be reached\n"));
            return ERROR;
        }
    }
    mutex_unlock(&bm_mutex);
    //TESTING(printk("No free entry!\n"));
    return ERROR;

}







