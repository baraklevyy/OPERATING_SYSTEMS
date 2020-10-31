#include <stdio.h>
#include "os.h"

void dozen_zero_padding(uint64_t *physical_address);
void load_levels(uint64_t *levels_array, uint64_t vpn);

uint64_t page_table_query(uint64_t pt, uint64_t vpn){
    uint64_t levels[5];
    load_levels(levels, vpn);
    for(int i = 0; i < 5; i++){
        /*phys_to_virt shift the first 12 bits right*/
        uint64_t current_physical_address = pt;
        dozen_zero_padding(&current_physical_address);
        void *virtual_address = phys_to_virt(current_physical_address);
        /*each PTE is of 8 bytes so offset has to duplicate by 8 bytes*/
        virtual_address += (levels[i] * 8);
        int valid = (*(uint64_t*)(virtual_address) & 0x1);
        if(valid == 0) {
            return NO_MAPPING;
        }
        pt = (*(uint64_t*)(virtual_address)) >> 12;
    }
    return pt;
}

void load_levels(uint64_t *levels_array, uint64_t vpn){
    for(int i = 0; i < 5; i++){
        *(levels_array + 4 - i) = (uint64_t)vpn & (0x1ff);
        vpn >>= (uint64_t)9;
    }
}
void dozen_zero_padding(uint64_t *physical_address){
    *(physical_address) <<= 12;
}
