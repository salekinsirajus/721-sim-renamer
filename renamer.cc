#include "renamer.h"
#include <stdexcept>

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
    int i;
    //TODO: initiate all fields to 0 in the active list entry
    for (i=0; i<n_active; i++){
        al.list[i] = {};
    }
    
}

bool renamer::is_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    return prf_ready[phys_reg];
}

bool renamer::stall_branch(uint64_t bundle_branch){
    //TODO: IS THIS THE RIGHT ALGORITHM?
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

void renamer::commit(){
    
    return;
}

void renamer::squash(){
    //TODO: Not Implemented
    //What does squashing entail?
    //zero out all the instructions in AL? or put the HEAD and TAIL together?
    //copy AMT to RMT
    //What else is involved in squashing a renamer with AMT+RMT?
    
    return;
}

void renamer::set_exception(uint64_t AL_index){
    this->al.list[AL_index].exception = 1;
}

void renamer::set_load_violation(uint64_t AL_index){
    this->al.list[AL_index].load_violation = 1;
}

void renamer::set_branch_misprediction(uint64_t AL_index){
    this->al.list[AL_index].br_mispredict = 1;
}

void renamer::set_value_misprediction(uint64_t AL_index){
    this->al.list[AL_index].val_mispredict = 1;
}

bool renamer::get_exception(uint64_t AL_index){
    //TODO: throw exception if AL_index is invalid
    return this->al.list[AL_index].exception; 
}
