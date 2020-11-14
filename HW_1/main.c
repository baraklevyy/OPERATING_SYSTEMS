#include "os.h"
#define LAST_LEVEL 4
#define DELETED_ADDRESS 0
#define FALSE 0
typedef uint64_t uint;
void load_levels(uint *levels_array, int size, uint vpn);
int is_valid(uint *virtual_address);
void* virtual_address_calc(uint current_physical_address, uint *levels, int i);



void page_table_update(uint pt, uint vpn, uint ppn){
    uint64_t  *levels;
    load_levels(levels,5, vpn);
    /*destroy memory mapping*/
    if(ppn == NO_MAPPING){
        for(int i = 0; i < 5; i++){
            uint64_t current_physical_address = (pt << 12);
            void *virtual_address = virtual_address_calc(current_physical_address, levels, i);
            if(is_valid(virtual_address) == FALSE) return;
            if(i != LAST_LEVEL) pt = (*(uint*)(virtual_address)) >> 12;
            else *(uint*)(virtual_address) = DELETED_ADDRESS;
        }
    }
    /*create memory mapping*/
    else{
        for(int i = 0; i < LAST_LEVEL; i++){
            uint current_physical_address = (pt << 12);
            void *virtual_address = virtual_address_calc(current_physical_address, levels, i);
            if(is_valid(virtual_address) == FALSE){
                pt = alloc_page_frame();
                /*updating virtual address to the corresponding physical address */
                *(uint*)(virtual_address) = pt << 12;
                /*set valid bit into TRUE*/
                *(uint*)(virtual_address) += (0x001);
            }
            else  pt = (*(uint*)(virtual_address)) >> 12;
        }
        /*last level updating*/
        uint current_physical_address = (pt << 12);
        void *virtual_address = virtual_address_calc(current_physical_address, levels, LAST_LEVEL);
        (*(uint*)(virtual_address)) = (ppn << 12) + (0x001);
    }
}

uint64_t page_table_query(uint pt, uint vpn){
    uint *levels;
    load_levels(levels,5, vpn);
    for(int i = 0; i < 5; i++){
        /*phys_to_virt shift the first 12 bits right*/
        uint current_physical_address = (pt << 12);
        void *virtual_address = virtual_address_calc(current_physical_address, levels, i);
        if(is_valid(virtual_address) == FALSE) {
            return NO_MAPPING;
        }
        pt = (*(uint*)(virtual_address)) >> 12;
    }
    return pt;
}

void load_levels(uint64_t *levels_array, int size, uint vpn){
    for(int i = 0; i < size; i++){
        *(levels_array + size - 1 - i) = vpn & (0x1ff);
        vpn >>= 9;
    }
}
int is_valid(uint *virtual_address){
    if((*(uint*)(virtual_address) & 0x1) == FALSE){
        return 0;
    }
    return 1;
}
void* virtual_address_calc(uint current_physical_address, uint *levels, int i){
    void *virtual_address = phys_to_virt(current_physical_address);
    /*each PTE is of 8 bytes so offset has to duplicate by 8 bytes. void pointer does not have explicitly no size of increment*/
    virtual_address += (levels[i] * 8);
    return virtual_address;
}
