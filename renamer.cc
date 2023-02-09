#include "renamer.h"

renamer::renamer(uint64_t n_log_regs,
        uint64_t n_phys_regs,
        uint64_t n_branches,
        uint64_t n_active){
    
    //sanitize inputs TODO
  
    //initialize the data structures
    rmt = new int[n_log_regs]; 
    amt = new int[n_log_regs];
    prf = new uint64_t[n_phys_regs];
    prf_ready = new uint64_t[n_phys_regs];

    //active list
    al.list = new al_entry[n_active]; 
    al.head = 0;
    al.tail = 0;
    al.head_phase = 0;
    al.tail_phase = 0;    
    //TODO: initiate all fields to 0 in the active list entry
}

void renamer::commit(){
    return;
}
bool renamer::stall_branch(uint64_t bundle_branch){
    //if the number of zero's in GBM is less than bundle_branch return true
    //return false otherwise TODO: is there multiple value of GBM
    int one_bit_counter;
    int gbm_copy = this->GBM;

    while (gbm_copy){
        if (gbm_copy & 1){
            one_bit_counter++;
            gbm_copy >>= 1;
        }
    }

    if (sizeof(this->GBM) - one_bit_counter < bundle_branch){
        return false;
    }

    return true;
} 
