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
    int i;
    //TODO: initiate all fields to 0 in the active list entry
    for (i=0; i<n_active; i++){
        al.list[i] = {};
    }
    
    //free list
    //free list size (721ss-prf-2 slide, p19), eq prf - n_log_regs
    free_list_size = n_phys_regs - n_log_regs;
    fl.list = new int[free_list_size];
    fl.head = 0;
    fl.tail = 0;
    fl.head_phase = 0;
    fl.tail_phase = 0;
    //What should be the content of the free list?
    for (i=0; i <free_list_size; i++){
        fl.list[i] = 0;
    }
}

int renamer::get_free_reg_count(free_list_t *free_list, int free_list_size){
    //TODO: MAKE SURE THIS IS WORKING
    //FIXME: double check the case when tail is increamented while the list
    //       is empty
    //case 1: head and tail are at the same location
    if (free_list->head == free_list->tail){
        if (free_list->head_phase != free_list->tail_phase){
            return 0;
        } else {
            return free_list_size;
        }
    }
    //case 2: head and tail are at different locations
    //between H and T: free regs (adjust based on the rotations)
    //between T and H: occupied in the AL
    int diff = free_list->tail - free_list->head;
    if (diff < 0){
        diff += free_list_size;
    }
    
    return diff;
}

bool renamer::stall_reg(uint64_t bundle_dst){
    //how do we know how many free physical registers
    //are available? Is it the free list? TODO: verify
    //if the number of available registers are less than
    //the input return false, otherwise return true

    int available_physical_regs = this->get_free_reg_count(&this->fl, this->free_list_size);
    if (available_physical_regs < bundle_dst){
        return false;
    }
    
    return true;
}

uint64_t renamer::rename_rdst(uint64_t log_reg){
    //phys. dest. reg. = pop new mapping from free list
    //RMT[logical dest. reg.] = phys. dest. reg.

    uint64_t result;
    if (this->get_free_reg_count(&this->fl, this->free_list_size) > 0){
        //read out the content of head of free list
        result = this->fl.list[this->fl.head];
        //increase head pointer of free list
        this->fl.head++;
        if (this->fl.head == this->free_list_size){
            //wrap around
            this->fl.head = 0;
            this->fl.head_phase = !this->fl.head_phase;
        }
        //update RMT
        this->rmt[log_reg] = result; 
    } else {
        printf("FATAL ERROR: rename_rdst - not enough free list entry\n");
        exit(EXIT_FAILURE);
    }
   
    return result; 
}

bool renamer::is_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    return this->prf_ready[phys_reg];
}

void renamer::clear_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    this->prf_ready[phys_reg] = 0;
}

void renamer::set_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    this->prf_ready[phys_reg] = 1;
}

uint64_t renamer::read(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    return this->prf[phys_reg];
}

void renamer::write(uint64_t phys_reg, uint64_t value){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    this->prf[phys_reg] = value; 
    //TODO: does this involve ALSO setting the ready bit?
    //TODO: look into this more
}

void renamer::set_complete(uint64_t AL_index){
    //TODO: check validity of the input
    this->al.list[AL_index].completed = 1; 
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

bool renamer::active_list_is_empty(){
    if ((this->al.head == this->al.tail) && 
        (this->al.head_phase == this->al.tail_phase)){

        return true;
    }

    return false;
}
