#include "renamer.h"
#include <cassert>

renamer::renamer(uint64_t n_log_regs,
        uint64_t n_phys_regs,
        uint64_t n_branches,
        uint64_t n_active){
    
    //Run the assertions
    assert(n_phys_regs > n_log_regs);
    //TODO: fix the machinary that sets the number of checkpoints to be flexible
    assert((1 <= n_branches) && (n_branches <= 64));
    assert(n_active > 0);
  
    //initialize the data structures
    map_table_size = n_log_regs;
    num_phys_reg = n_phys_regs;
    rmt = new uint64_t[n_log_regs]; 
    amt = new uint64_t[n_log_regs];
    prf = new uint64_t[n_phys_regs];
    prf_ready = new uint64_t[n_phys_regs];
    shadow_map_table_size = n_log_regs;

    int i;
    //setting the ready bits to 1 (meaning no pending registers)
    for (i=0; i < n_phys_regs; i++){
        prf_ready[i] = 1;
        //AMT and RMT should have the same value at the beginning
        //However, not sure what would the content be. if amt[0] = 0, amt[1] = 0
        //then all the logical registers are mapped to p0. OTOH, amt[0] = 0 and
        //amt[1] = 1,..., amt[n] = n indicate r0->p0, r1->p1,..
        //the contents of the prf does not matter I suppose.
        amt[i] = i;
        rmt[i] = i;
    }

    //active list
    active_list_size = n_active;
    al.list = new al_entry[n_active]; 
    al.head = 0;
    al.tail = 0;
    al.head_phase = 0;
    al.tail_phase = 0;    
    assert(active_list_is_empty());

    for (i=0; i<n_active; i++){
        init_al_entry(&al.list[i]);
    }
    
    //free list
    //free list size (721ss-prf-2 slide, p19), eq prf - n_log_regs
    free_list_size = n_phys_regs - n_log_regs;
    fl.list = new uint64_t[free_list_size];
    fl.head = 0;
    fl.tail = 0;
    fl.head_phase = 0;
    fl.tail_phase = 1;
    assert(this->free_list_is_full()); //free list should be full at init

    //FIXME: (potential issues) What should be the content of the free list?
    for (i=0; i <free_list_size; i++){
        fl.list[i] = i; //these should be unique values
    }

    //checkpoint stuff
    //how many different checkpoint entrys? 
    //should we keep this in a container?   
    GBM = 0;
    num_checkpoints = sizeof(uint64_t)*8;
    checkpoints = new checkpoint_t[num_checkpoints];

}

int renamer::free_list_regs_available(){
    if (this->free_list_is_full()) return this->free_list_size;
    if (this->free_list_is_empty()) return 0;

    //only in two case we have the correct result
    // T > H and HP == TP, (T-H) available
    // T < H and TP - HP == 1, (T-H + size) available
    int result;
    if ((this->fl.tail > this->fl.head) && (this->fl.tail_phase == this->fl.head_phase)){
        result = this->fl.tail - this->fl.head; 
    } 
    
    else if ((this->fl.tail < this->fl.head) && (this->fl.tail_phase > this->fl.head_phase)){
        result = this->fl.tail - this->fl.head + this->free_list_size;
    }
    
    else {
        //Inconsistent state
        result = -1;
    }

    return result;
}

bool renamer::stall_reg(uint64_t bundle_dst){
    //how do we know how many free physical registers
    //are available? Is it the free list? TODO: verify
    //if the number of available registers are less than
    //the input return false, otherwise return true

    int available_physical_regs = this->free_list_regs_available();
    if (available_physical_regs == -1) {
        printf("FATAL ERROR: free list is in incosistent state\n");
        exit(EXIT_FAILURE);
    }

    if (available_physical_regs < bundle_dst){
        printf("renamer::stall_reg() return true\n");
        return true;
    }
    
    printf("renamer::stall_reg() return false\n");
    return false;
}


uint64_t renamer::rename_rsrc(uint64_t log_reg){
    //read off of RMT. This provides the current mapping
    //TODO: double check this is how the src reg renaming works
    printf("renamer::rename_rsrc(%d) = %d\n", log_reg, this->rmt[log_reg]);
    printf("renamer::rename_rsrc(%d) = amt[%d], rmt[%d]\n", log_reg, this->amt[log_reg], this->rmt[log_reg]);
    return this->rmt[log_reg]; 
}

bool renamer::free_list_is_empty(){
    if ((this->fl.head == this->fl.tail) && 
        (this->fl.head_phase == this->fl.tail_phase)) return true;

    return false;
}

bool renamer::free_list_is_full(){
    if ((this->fl.head == this->fl.tail) && 
        (this->fl.head_phase != this->fl.tail_phase)) return true;

    return false;
}

void renamer::restore_free_list(){
    //TODO/FIXME: test this works
    //restore free list
    this->fl.head = this->fl.tail;
    this->fl.head_phase = !this->fl.tail_phase;
}

bool renamer::push_free_list(uint64_t phys_reg){
    //if it's full, you cannot push more into it
    if (this->free_list_is_full()){
        return false;
    }

    this->fl.list[this->fl.tail] = phys_reg;
    //  - advance tail ptr
    this->fl.tail++;
    if (this->fl.tail == this->free_list_size){
        this->fl.tail = 0;
        this->fl.tail_phase = !this->fl.tail_phase;
    }
  
    return true; 
}

uint64_t renamer::pop_free_list(){
    //TODO: if free list is empty do not return anything
    if (this->free_list_is_empty()){
        return UINT64_MAX;
    }

    uint64_t result;
    result = this->fl.list[this->fl.head];

    //increase head pointer of free list
    this->fl.head++;
    if (this->fl.head == this->free_list_size){
        //wrap around
        this->fl.head = 0;
        this->fl.head_phase = !this->fl.head_phase;
    }

    return result;

}
uint64_t renamer::rename_rdst(uint64_t log_reg){
    //phys. dest. reg. = pop new mapping from free list
    //RMT[logical dest. reg.] = phys. dest. reg.
    printf("renamer::rename_rdst() called\n");

<<<<<<< HEAD
    uint64_t result = this->pop_free_list();
    if (result == UINT64_MAX){
=======
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
        //printf("RMT+PRF before updating with popped register from the free list\n");
        //this->print_rmt();
        //this->print_prf();
        this->rmt[log_reg] = result; 
        //printf("RMT+PRF after updating with popped register from the free list\n");
        //this->print_rmt();
        //this->print_prf();
    } else {
>>>>>>> Added more debugging statements
        printf("FATAL ERROR: rename_rdst - not enough free list entry\n");
        exit(EXIT_FAILURE);
    } else {
        //update RMT
        this->rmt[log_reg] = result; 
    }
   
    //FIXME: what happens to the old mapping?????
    printf("renamer::rename_rdst(%d) = amt[%d], rmt[%d]\n", log_reg, this->amt[log_reg], this->rmt[log_reg]);
    printf("renamer::rename_rdst(%d) = %d\n", log_reg, this->rmt[log_reg]);
    return result; 
}

int renamer::allocate_gbm_bit(){
    //NOTE: Allocate free bit from left to right, so if i<j and both of bits
    //      are zero, it will return i
    //TODO: is this the right approach for resolving?
    uint64_t gbm = this->GBM;
    if (gbm == UINT64_MAX) return -1;
    
    int i, n = sizeof(this->GBM) * 8;
    uint64_t mask = 1;

    for (i=0; i < n; i++){
        if (gbm & mask){
            mask <<= 1;
        } else {
            return i;
        }
    }

    return -1; //No free bit found
}

uint64_t renamer::get_branch_mask(){
    //    An instruction's initial branch mask is the value of the
    //    the GBM when the instruction is renamed.
    //FIXME: is it always suppossed to return the GBM or sth else based on
    //    where the instruction is
    
    return this->GBM;
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
    temp.__in_use = true;
    temp.gbm = this->GBM;

    //FIXME: is this where the new checkpoint should go?
    uint64_t gbm_bit = this->allocate_gbm_bit();
    if ((gbm_bit < 0) | (gbm_bit > 63)){
        printf("This should not happen. Could not allocate checkpoint, should stall\n");
        exit(EXIT_FAILURE);
    }

    this->checkpoints[gbm_bit] = temp;
    //mark this position taken at the GBM
    this->GBM |= (1ULL<<gbm_bit);

    //  4. Checkpointed GBM
    return gbm_bit;
}

uint64_t renamer::insert_into_active_list(){
    //if it's full, you cannot push more into it
    if (this->active_list_is_full()){
        return UINT64_MAX; 
    }

    uint64_t insertion_point = this->al.tail;

    this->al.tail++;
    if (this->al.tail == this->active_list_size){
        this->al.tail = 0;
        this->al.tail_phase = !this->al.tail_phase;
    }
  
    return insertion_point;
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
    if (this->stall_dispatch(true)){
        //well HOW DO YOU ACTUALLY STALL DISPATCH??
        //FIXME: the following is wrong, used as a placeholder
        exit(EXIT_FAILURE);
    }
    printf("renamer::dispatch_inst() is called\n");

    uint64_t idx_at_al = this->insert_into_active_list(); 
    if (idx_at_al == UINT64_MAX){
        //assert active list is not full
        //assert(!this->active_list_is_full());
        printf("FATAL ERROR: cannot insert when active list is full\n");
        exit(EXIT_FAILURE);
    }

    //make a new active list entry
    al_entry_t *active_list_entry;
    active_list_entry = &this->al.list[idx_at_al]; //WIP

    active_list_entry->has_dest = dest_valid;
    if (dest_valid == true){
        active_list_entry->logical = log_reg;
        active_list_entry->physical = phys_reg;
    } else {
        active_list_entry->logical = 67; //this should not be causing problems
        active_list_entry->physical = 67; //this should not be causing problems
    }

    active_list_entry->completed = 0; //just dispatched
    active_list_entry->exception = 0; //just dispatched
    active_list_entry->is_load = load;
    active_list_entry->is_store = store;
    active_list_entry->is_branch = branch;
    active_list_entry->is_amo = amo;
    active_list_entry->is_csr = csr;
    active_list_entry->pc = PC;
    //saving the index to return at the end of the function
    

    return idx_at_al;
}

bool renamer::stall_dispatch(uint64_t bundle_dst){
    //Assert Active List is not full
    if ((this->al.head == this->al.tail) && 
        (this->al.head_phase != this->al.tail_phase)){
        printf("Active List is full, call stall_dispatch\n");
        return true;
    }
   
    //WIP: find how many entries are available in the Active List  
    if (this->get_free_al_entry_count() < bundle_dst){
        printf("Number of free entries are less than what's needed\n");
        return true;
    }

    return false;
}

bool renamer::is_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    printf("renamer::is_ready(%d) = %d\n", phys_reg, this->prf_ready[phys_reg]);
    return this->prf_ready[phys_reg];
}

void renamer::clear_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    printf("renamer::clear_ready(%d) = %d (before)\n", phys_reg, this->prf_ready[phys_reg]);
    this->prf_ready[phys_reg] = 0;
    printf("renamer::clear_ready(%d) = %d (after)\n", phys_reg, this->prf_ready[phys_reg]);
}

void renamer::set_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    printf("renamer::set_ready(%d) = %d (before)\n", phys_reg, this->prf_ready[phys_reg]);
    this->prf_ready[phys_reg] = 1;
    printf("renamer::set_ready(%d) = %d (after)\n", phys_reg, this->prf_ready[phys_reg]);
}

uint64_t renamer::read(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    printf("renamer::read()   prf[%d]=%d\n", phys_reg, this->prf[phys_reg]);
    return this->prf[phys_reg];
}

void renamer::write(uint64_t phys_reg, uint64_t value){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    printf("renamer::write() prf[%d]=%d\n", phys_reg, value);
    this->prf[phys_reg] = value; 
    //TODO: does this involve ALSO setting the ready bit?
    //TODO: look into this more
}

void renamer::set_complete(uint64_t AL_index){
    printf("renamer::set_complete() AL index: %d\n", AL_index);
 
    //TODO: check validity of the input
    this->al.list[AL_index].completed = 1; 
    //this->print_active_list(0);
    //this->print_rmt();
    //this->print_amt();
}

bool renamer::stall_branch(uint64_t bundle_branch){
    //TODO: IS THIS THE RIGHT ALGORITHM?
    //if the number of zero's in GBM is less than bundle_branch return true
    //return false otherwise TODO: is there multiple value of GBM
    printf("renamer::stall_branch() is called\n");
    int one_bit_counter = 0;
    int gbm_copy = this->GBM;

    while (gbm_copy){
        if (gbm_copy & 1){
            one_bit_counter++;
            gbm_copy >>= 1;
        }
    }

    if ((sizeof(this->GBM) * 8) - one_bit_counter < bundle_branch){
        return true;
    }

    return false;
} 

bool renamer::precommit(bool &completed,
                        bool &exception, bool &load_viol, bool &br_misp,
                        bool &val_misp, bool &load, bool &store,
                        bool &branch, bool &amo, bool &csr,
                        uint64_t &PC){

    printf("renamer::precommit() is called\n");

    if (this->active_list_is_empty()){
        printf("renamer::precommit() - active list is empty.\n");
        return false; 
    }

    //set the flags
    al_entry_t *head;
    head = &this->al.list[this->al.head];
    
    printf("precommit(): before - completed: %d, exception: %d, is_load: %d, is_store: %d, pc: %lld\n",
            completed, exception, load, store, PC);

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

    printf("precommit(): after - completed: %d, exception: %d, is_load: %d, is_store: %d, pc: %lld\n",
            completed, exception, load, store, PC);

    return true;
}

void renamer::commit(){
    printf("renamer::commit() is called\n");
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
    //EXCEPTION: only if the instruction has a valid destination
    bool op;
    if (al_head->has_dest == true){
        printf("commit(): insn being committed: completed: %d, exc: %d, is_load: %d, is_store: %d, has_dest: %d\n",
                al_head->completed, al_head->exception, al_head->is_load, al_head->is_store, al_head->has_dest
        );
        uint64_t old_mapping = this->amt[al_head->logical];
        printf("commit(): old mapping at AMT: %d\n", this->amt[al_head->logical]);

        //Update AMT with with new mapping 
        this->amt[al_head->logical] = al_head->physical;

        printf("commit(): new mapping at AMT: %d\n", this->amt[al_head->logical]);

        op = this->push_free_list(old_mapping);
        if (op == false){
            printf("FATAL ERROR: tried to push when the free list is full\n");
            exit(EXIT_FAILURE);
        }
    }

    //TODO: update other structures like AMT, RMT, Free list, Shadow Map Table?
    op = this->retire_from_active_list();
    if (op == false){
        printf("FATAL ERROR: could not retire from AL since its empty\n");
        exit(EXIT_FAILURE);
    }

    return;
}

bool renamer::retire_from_active_list(){
    //TODO: if free list is empty do not return anything
    if (this->active_list_is_empty()){
        return false;
    }

    //TODO: updating other structures like AMT, RMT, Free list, Shadow Map Table?
    //advance head pointer of the Active List
    ////printf("printing active list before increamenting the head pointer\n");
    ////this->print_active_list(0);
    this->al.head++;
    if (this->al.head == this->active_list_size){
        //wrap around
        this->al.head = 0;
        this->al.head_phase = !this->al.head_phase;
    }
    ////printf("printing active list after increamenting the head pointer\n");
    ////this->print_active_list(0);
    ////this->print_rmt();
    ////this->print_amt();
    
    return true;
}

void renamer::resolve(uint64_t AL_index, uint64_t branch_ID, bool correct){
    printf("renamer::resolve() is called\n");
    if (correct){ //branch was predicted correctly
        //clear the GBM bit by indexing with branch_ID
        this->GBM  |= (1ULL<<branch_ID);
        //clear all the checkpointed GBMs;
        int i;
        for (i=0; i < this->num_checkpoints; i++){
            this->checkpoints[i].gbm |= (1ULL << branch_ID);
        } 
        //TODO: (verify) resetting the in_use to false
        this->checkpoints[branch_ID].__in_use = false; 

    } else {
        //FIXME: Not Implemented
        printf("resolve():: Branch Mispredict recovery Not Implemented\n");
    }
}


void renamer::squash(){
    //FIXME: Not Implemented
    //What does squashing entail?
    //zero out all the instructions in AL. AL tail = head
    printf("renamer::squash is called\n");
    //empty out the active list
    this->al.head = this->al.tail;
    this->al.head_phase = this->al.tail_phase;
    
    this->restore_free_list();

    //copy AMT to RMT
    int i;
    for (i=0; i < this->map_table_size; i++){
        rmt[i] = amt[i];
    }
    
    //What else is involved in squashing a renamer with AMT+RMT?
    printf("squash gets called. Not Implemented\n");
    
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


int renamer::get_free_al_entry_count(){
    if (this->active_list_is_full()) return 0;
    if (this->active_list_is_empty()) return this->active_list_size;

    if (this->al.head_phase != this->al.tail_phase){
        //inconsistent state otherwise
        assert(this->al.head > this->al.tail);
        //h(1), t(0)
        if (this->al.head_phase > this->al.tail_phase){
            //available
            return this->al.head - this->al.tail;
        } else { //h(0), t(1)
            //available
            return this->al.tail - this->al.head;
        }
    }

    if (this->al.head_phase == this->al.tail_phase){
        assert(this->al.tail > this->al.head);
        // t-h = occupied
        return this->active_list_size - (this->al.tail - this->al.head);
    }

}

bool renamer::active_list_is_full(){
    if ((this->al.head == this->al.tail) && 
        (this->al.head_phase != this->al.tail_phase)){

        return true;
    }

    return false;
}

bool renamer::active_list_is_empty(){
    if ((this->al.head == this->al.tail) && 
        (this->al.head_phase == this->al.tail_phase)){

        return true;
    }

    return false;
}

void renamer::print_active_list(bool between_head_and_tail){
    int i=0, n=active_list_size;
    if (between_head_and_tail) {
        i = this->al.head;
        n = this->al.tail;
    }
    printf("| idx | log| phys | com | exc |\n");
    for (i; i < n; i++){
        al_entry_t *t;
        t = &this->al.list[i];
        printf("| %3d | %3d | %3d | %3d | %3d |\n", i, t->logical, t->physical, t->completed, t->exception);   
    }
    printf("AL Head: %d, AL tail: %d, Head Phase: %d, Tail Phase: %d\n",
            this->al.head, this->al.tail, this->al.head_phase, this->al.tail_phase
        );

}

void renamer::init_al_entry(al_entry_t *ale){
    //WIP: fix the type
    //initiate all fields to 0 
    ale->has_dest=0;
    ale->logical=0;
    ale->physical=0;
    ale->completed=0;
    ale->exception=0;
    ale->load_violation=0;
    ale->br_mispredict=0;
    ale->val_mispredict=0;
    ale->is_load=0;
    ale->is_store=0;
    ale->is_branch=0;
    ale->is_amo=0;
    ale->is_csr=0;
    ale->pc=0;
}

//Debugging helper functions
void renamer::print_free_list(){}
void renamer::print_amt(){
    printf("---------------------AMT-----------------\n");
    for (int i=0; i < this->map_table_size; i++){
        printf("| %3d ", amt[i]);
    }
    printf("\n-------------------END_AMT-----------------\n");
}
void renamer::print_rmt(){
    printf("---------------------RMT-----------------\n");
    for (int i=0; i < this->map_table_size; i++){
        printf("| %3d ", rmt[i]);
    }
    printf("\n-------------------END_RMT-----------------\n");

}
void renamer::print_prf(){
    printf("---------------------PRF-----------------\n");
    for (int i=0; i < this->num_phys_reg; i++){
        printf("| %16d ", prf[i]);
    }
    printf("\n-------------------END_PRF-----------------\n");

}
