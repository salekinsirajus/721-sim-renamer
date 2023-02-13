#include "renamer.h"
#include <cassert>

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
    shadow_map_table_size = n_log_regs;

    //active list
    active_list_size = n_active;
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

    //checkpoint stuff
    //how many different checkpoint entrys? 
    //should we keep this in a container?   
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


uint64_t renamer::rename_rsrc(uint64_t log_reg){
    //read off of RMT. This provides the current mapping
    //TODO: double check this is how the src reg renaming works
    return this->rmt[log_reg]; 
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

int renamer::allocate_gbm_bit(){
    if (this->GBM == UINT64_MAX){
        return -1; //not available
    }

    //iterate until you find an empty one
    uint64_t gbm = this->GBM;
    int i=0;
    while (i < sizeof(uint64_t) * 8){
        if (gbm & 0 == 0){
            //found an empty bit
            return i;
        }
        gbm =>> 1;
        i++
    }

    return -1;
}

uint64_t renamer::checkpoint(){
    //create a new branch checkpoint
    //allocate:
    //  1. GMB bit: starting from left or right? which way does this move? 
    // 

    //  2. Shadow Map Table (checkpointed RMT)
    cp temp;
    // SMT: copying over the AMT content
    temp.shadow_map_table = new int[this->shadow_map_table_size];
    int i;
    for (i=0; i<this->shadow_map_table_size; i++){
        temp.shadow_map_table[i] = this->amt[i];
    }
    // SMT: head and head phase
    temp.free_list_head = this->fl.head;
    temp.free_list_head_phase = this->fl.head_phase;
    temp.gbm = this->GBM;

    //FIXME: is this where the new checkpoint should go?
    this->checkpoints.push_back(temp);

    //  4. Checkpointed GBM
}

uint64_t renamer::dispatch_inst(bool dest_valid,
                           uint64_t log_reg,
                           uint64_t phys_reg,
                           bool load,
                           bool store,
                           bool branch,
                           bool amo,
                           bool csr,
                           uint64_t PC){
    /* Mechanism: Reserve entry at tail, write the instruction's logical
      and physical destination register specifiers, increment tail pointer
    */
    if (this->stall_dispatch(1)){
        //well HOW DO YOU ACTUALLY STALL DISPATCH??
        //FIXME: the following is wrong, used as a placeholder
        exit(EXIT_FAILURE);
    }

    //make a new active list entry
    al_entry_t *active_list_entry;
    active_list_entry = &this->al.list[this->al.tail];
    active_list_entry->has_dest = dest_valid;
    active_list_entry->logical = (dest_valid) ? log_reg: 0;
    active_list_entry->physical = (dest_valid) ? phys_reg: 0;
    active_list_entry->is_load = load;
    active_list_entry->is_store = store;
    active_list_entry->is_branch = branch;
    active_list_entry->is_amo = amo;
    active_list_entry->is_csr = csr;
    active_list_entry->pc = PC;
    //saving the index to return at the end of the function
    uint64_t idx_at_al = this->al.tail; 

    this->al.tail++;
    //wrap around of al.tail is at the size of AL
    if (this->al.tail == this->active_list_size){
        this->al.tail = 0;
        this->al.tail_phase = !this->al.tail_phase;
    }

    return idx_at_al;
}

bool renamer::stall_dispatch(uint64_t bundle_dst){
    //Assert Active List is not full
    if ((this->al.head == this->al.tail) && 
        (this->al.head_phase != this->al.tail_phase)){
        //Active List is full, call stall_dispatch 
        return true;
    }
   
    //WIP: find how many entries are available in the Active List  
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

bool renamer::precommit(bool &completed,
                        bool &exception, bool &load_viol, bool &br_misp,
                        bool &val_misp, bool &load, bool &store,
                        bool &branch, bool &amo, bool &csr,
                        uint64_t &PC){
    if (this->active_list_is_empty()){
        return false; 
    }

    //set the flags
    al_entry_t *head;
    head = &this->al.list[this->al.head];

    completed = head->completed; 
    exception = head->exception;
    load_viol = head->load_violation;
    br_misp   = head->br_mispredict;
    val_misp  = head->val_mispredict;
    load      = head->is_load;
    store     = head->is_store;
    branch    = head->is_branch;
    amo       = head->is_amo;
    csr       = head->is_csr;
    PC        = head->pc;

    return true;
}

void renamer::commit(){
    //assertion: active list not empty
    assert(!this->active_list_is_empty());     
    al_entry_t *al_head;
    al_head = &this->al.list[this->al.head];
    //FIXME: make sure the satements like above is returning the correct stuff

    //assert different bits are correct 
    assert(al_head->completed);
    assert(!al_head->exception);
    assert(!al_head->load_violation);
    
    //commit the instruction at the head of the active list
    //find the physical dst register by looking up the AMT with logical reg
    int to_be_freed = this->amt[al_head->logical]; 
    //push the freed reg to the free list 
    //  - insert at it the tail
    
    this->fl.list[this->fl.tail] = to_be_freed;
    //  - advance tail ptr
    this->fl.tail++; 
    if (this->fl.tail == this->free_list_size){
        this->fl.tail = 0;
        this->fl.tail_phase = !this->fl.head_phase;
    }

    //TODO: updating other structures like AMT, RMT, Free list, Shadow Map Table?
    //advance head pointer of the Active List
    this->al.head++;
    //FIXME: do we wrap the active list the same way we do freelist?
    if (this->al.tail == this->active_list_size){
        this->al.tail = 0;
        this->al.tail_phase = !this->al.head_phase;
    }
    
    return;
}

void renamer::resolve(uint64_t AL_index, uint64_t branch_ID, bool correct){
    
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
