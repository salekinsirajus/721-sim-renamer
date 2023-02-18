#define __STDC_FORMAT_MACROS

#include "renamer.h"
#include <cassert>

renamer::renamer(uint64_t n_log_regs,
        uint64_t n_phys_regs,
        uint64_t n_branches,
        uint64_t n_active){

    retired_insn = 0; //FIXME: delete this once you are done
    
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

    uint64_t j;
    //setting the ready bits to 1 (meaning no pending registers)
    for (j=0; j < n_phys_regs; j++){
        prf_ready[j] = 1;
        //prf[i] = i; //New addition 
        //AMT and RMT should have the same value at the beginning
        //However, not sure what would the content be. if amt[0] = 0, amt[1] = 0
        //then all the logical registers are mapped to p0. OTOH, amt[0] = 0 and
        //amt[1] = 1,..., amt[n] = n indicate r0->p0, r1->p1,..
        //the contents of the prf does not matter I suppose.
    }

    for (j=0; j < n_log_regs; j++){
        amt[n_log_regs - 1 - j] = j;
        rmt[n_log_regs - 1 - j] = j;
    }

    //active list
    active_list_size = n_active;
    al.list = new al_entry[n_active]; 
    al.head = 0;
    al.tail = 0;
    al.head_phase = 0;
    al.tail_phase = 0;    
    assert(active_list_is_empty());

    for (j=0; j<n_active; j++){
        init_al_entry(&al.list[j]);
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
    int i;
    for (i=0; i <free_list_size; i++){
        fl.list[i] = n_log_regs + i;
    }

    //checkpoint stuff
    //how many different checkpoint entrys? 
    //should we keep this in a container?   
    GBM = 0;
    num_checkpoints = sizeof(uint64_t)*8;
    checkpoints = new checkpoint_t[num_checkpoints];


    printf("----------Initial AMT and RMT-----------\n");
    print_amt();
    print_rmt();
    printf("----------End Initial AMT and RMT-----------\n");
    printf("----------Initial Free List---------------\n");
    print_free_list();
    printf("--------End Initial Free List-------------\n");
}

int renamer::free_list_regs_available(){
    if (this->free_list_is_full()) return this->free_list_size;
    if (this->free_list_is_empty()) return 0;
    
    uint64_t available = UINT64_MAX;
    if (this->fl.head_phase != this->fl.tail_phase){
        //otherwise inconsistent state: tail cannot be ahead of head,
        //means you are inserting entry when the list is already full
        assert(this->fl.head > this->fl.tail);
        available = this->fl.tail - this->fl.head + this->free_list_size;
        return available;
    }

    if (this->fl.head_phase == this->fl.tail_phase){
        //otherwise inconsistent state: head cant be ahead of tail, means
        // it allocated registers it does not have
        assert(this->fl.head < this->fl.tail);
        //available regsiters
        available = this->fl.tail - this->fl.head;
        return available;
    }


    return available; //it should never come here bc of the assertions 
}

bool renamer::stall_reg(uint64_t bundle_dst){
    //how do we know how many free physical registers
    //are available? Is it the free list? TODO: verify
    //if the number of available registers are less than
    //the input return false, otherwise return true

    uint64_t available_physical_regs = this->free_list_regs_available();
    if (available_physical_regs == UINT64_MAX) {
/*
        printf("Free List: head %d, tail %d, head_phase %d, tail_phase %d\n",
            fl.head, fl.tail, fl.head_phase, fl.tail_phase
        );
*/
        printf("FATAL ERROR: free list is in incosistent state\n");
        exit(EXIT_FAILURE);
    }

    if (available_physical_regs < bundle_dst){
    //    printf("renamer::stall_reg() return true\n");
        return true;
    }
    
    //printf("renamer::stall_reg() return false\n");
    return false;
}


uint64_t renamer::rename_rsrc(uint64_t log_reg){
    //read off of RMT. This provides the current mapping
    //TODO: double check this is how the src reg renaming works
/*
    printf("renamer::rename_rsrc(%d) = %d\n", log_reg, this->rmt[log_reg]);
    printf("renamer::rename_rsrc(%d) = amt[%d], rmt[%d]\n", log_reg, this->amt[log_reg], this->rmt[log_reg]);
*/
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
/*
    printf("push_free_list(): pushing prf[%llu]   to the free list\n", phys_reg);
*/
    this->fl.list[this->fl.tail] = phys_reg;
    //  - advance tail ptr
    this->fl.tail++;
    if (this->fl.tail == this->free_list_size){
        this->fl.tail = 0;
        this->fl.tail_phase = !this->fl.tail_phase;
    }
/*
    printf("FREELIST: Free register count after PUSH: %d\n", this->free_list_regs_available());
 */ 
    return true; 
}

uint64_t renamer::pop_free_list(){
    //TODO: if free list is empty do not return anything
    if (this->free_list_is_empty()){
        return UINT64_MAX;
    }

    uint64_t result;
    result = this->fl.list[this->fl.head];
/*
    printf("pop_free_list(): popping prf[%llu] from the free list\n", result);
*/
    //increase head pointer of free list
    this->fl.head++;
    if (this->fl.head == this->free_list_size){
        //wrap around
        this->fl.head = 0;
        this->fl.head_phase = !this->fl.head_phase;
    }
/*
    printf("FREELIST: Free register count after POP : %d\n", this->free_list_regs_available());
*/
    return result;

}
uint64_t renamer::rename_rdst(uint64_t log_reg){
    //phys. dest. reg. = pop new mapping from free list
    //RMT[logical dest. reg.] = phys. dest. reg.
/*
    printf("renamer::rename_rdst() called\n");
*/

    uint64_t result = this->pop_free_list();
    if (result == UINT64_MAX){
        printf("FATAL ERROR: rename_rdst - not enough free list entry\n");
        exit(EXIT_FAILURE);
    } else {
        //update RMT
        this->rmt[log_reg] = result; 
    }
   
    //FIXME: what happens to the old mapping?????
/*
    printf("renamer::rename_rdst(%d) = amt r%d[%d], rmt r%d[%d]\n", log_reg, log_reg, this->amt[log_reg], log_reg, this->rmt[log_reg]);
    printf("renamer::rename_rdst(%d) = %d\n", log_reg, this->rmt[log_reg]);
*/
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
    temp.shadow_map_table = new uint64_t[this->shadow_map_table_size];
    int i;
    for (i=0; i<this->shadow_map_table_size; i++){
        temp.shadow_map_table[i] = this->rmt[i];
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
    /*
    if (this->stall_dispatch(true)){
        //well HOW DO YOU ACTUALLY STALL DISPATCH??
        //FIXME: the following is wrong, used as a placeholder
        printf("We called stall dispatch - from dispatch_inst");
        exit(EXIT_FAILURE);
    }*/
    assert(!this->active_list_is_full());
/*
    printf("renamer::dispatch_inst() is called\n");
*/
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
        active_list_entry->logical = UINT64_MAX; //this should not be causing problems
        active_list_entry->physical = UINT64_MAX; //this should not be causing problems
    }

    active_list_entry->completed = false; //just dispatched
    active_list_entry->exception = false; //just dispatched
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
    //printf("stall_dispatch():\n");
    uint64_t available_al_entry = this->get_free_al_entry_count();
    //printf("stall_dispatch(): free AL entry: %d\n", available_al_entry);
    if (this->active_list_is_full()){
    //    printf("stall_dispatch(): Active List is full, stalling dispatch\n");
        return true;
    }
   
    //WIP: find how many entries are available in the Active List  
    if ((available_al_entry < bundle_dst) || (available_al_entry <= 0)){
     //   printf("stall_dispatch(): true - available: %d, needed: %d\n", available_al_entry, bundle_dst);
        return true;
    }

    //printf("stall_dispatch():: enough AL entry available, returning false\n");
    return false;
}

bool renamer::is_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
/*
    printf("renamer::is_ready(%d) = %d\n", phys_reg, this->prf_ready[phys_reg]);
*/
    return this->prf_ready[phys_reg];
}

void renamer::clear_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    this->prf_ready[phys_reg] = 0;
/*
    printf("renamer::clear_ready(%d) = %d (after)\n", phys_reg, this->prf_ready[phys_reg]);
*/
}

void renamer::set_ready(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
    this->prf_ready[phys_reg] = 1;
/*
    printf("renamer::set_ready(%d) = %d (after)\n", phys_reg, this->prf_ready[phys_reg]);
*/
}

uint64_t renamer::read(uint64_t phys_reg){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
/*
    printf("renamer::read()   prf[%d]=%d\n", phys_reg, this->prf[phys_reg]);
*/
    return this->prf[phys_reg];
}

void renamer::write(uint64_t phys_reg, uint64_t value){
    //TODO: check validity of the input
    //TODO: is this phys_reg 0 or 1 indexed?
/*
    printf("renamer::write() prf[%d]=%d\n", phys_reg, value);
*/
    this->prf[phys_reg] = value; 
    //TODO: does this involve ALSO setting the ready bit?
    //TODO: look into this more
}

void renamer::set_complete(uint64_t AL_index){
/*
    printf("renamer::set_complete() AL index: %d\n", AL_index);
 */
    //TODO: check validity of the input
    this->al.list[AL_index].completed = true; 
    //this->print_active_list(0);
    //this->print_rmt();
    //this->print_amt();
}

bool renamer::stall_branch(uint64_t bundle_branch){
    //TODO: IS THIS THE RIGHT ALGORITHM?
    //if the number of zero's in GBM is less than bundle_branch return true
    //return false otherwise TODO: is there multiple value of GBM
    //total_bits = num_checkpoints
/*
    printf("renamer::stall_branch() is called\n");
*/
    uint64_t gbm = this->GBM;
    //simple use case, all taken, DO STALL
    if (gbm == UINT64_MAX) return true;

    uint64_t free_count=0;
    uint64_t n = num_checkpoints;
    uint64_t mask = 1;
    
    uint64_t i;
    for (i=0; i<n; i++){
        if (gbm & mask){
            mask <<= 1;
        } else {
            mask <<= 1;
            free_count++;
        }
    }

    //not enough space, DO STALL
    if (free_count < bundle_branch) return true;
    
    //free_count >= bundle_branch, enough space available. DONT STALL
    return false;
} 

bool renamer::precommit(bool &completed,
                        bool &exception, bool &load_viol, bool &br_misp,
                        bool &val_misp, bool &load, bool &store,
                        bool &branch, bool &amo, bool &csr,
                        uint64_t &PC){

    //printf("renamer::precommit() is called\n");

    if (this->active_list_is_empty()){
       // printf("renamer::precommit() - active list is empty.\n");
        return false; 
    }

    //set the flags
    al_entry_t *head;
    head = &this->al.list[this->al.head];
    
    //printf("precommit(): before - completed: %d, exception: %d, is_load: %d, is_store: %d, pc: %lld\n",
    //completed, exception, load, store, PC);

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
/*
    printf("precommit(): after - completed: %d, exception: %d, is_load: %d, is_store: %d, pc: %lld\n",
            completed, exception, load, store, PC);
*/
    return true;
}

void renamer::commit(){
/*
    printf("renamer::commit() is called\n");
*/
    //assertion: active list not empty
    assert(!this->active_list_is_empty());     
    al_entry_t *al_head;
    al_head = &this->al.list[this->al.head];
    //FIXME: make sure the satements like above is returning the correct stuff

/*
    printf("retire(): prertire_checks - completed: %d, exception: %d, is_load: %d, is_store: %d, pc: %lld\n",
            al_head->completed, al_head->exception, al_head->is_load, al_head->is_store, al_head->pc);
    printf("retire(): retired index %d from the Active List\n", al.head);
*/
    //assert different bits are correct
    assert(al_head->completed == true);
    assert(al_head->exception == false);
    assert(al_head->load_violation == false);

    //commit the instruction at the head of the active list
    //find the physical dst register by looking up the AMT with logical reg
/*
    printf("commit(): insn being committed: completed: %d, exc: %d, is_load: %d, is_store: %d, has_dest: %d\n",
            al_head->completed, al_head->exception, al_head->is_load, al_head->is_store, al_head->has_dest
    );
*/
    //EXCEPTION: only if the instruction has a valid destination
    bool op;
    if (al_head->has_dest == true){
        assert(this->free_list_is_full() != true);
        //printf("commit(): AL head at (before): %d\n", this->al.head);
        //printf("commit(): befor PRF[%d]=%llu, PRF_READY[%d]=%llu\n",
        //al_head->physical, this->prf[al_head->physical], al_head->physical, this->prf_ready[al_head->physical]);
        uint64_t old_mapping = this->amt[al_head->logical];
        //printf("commit(): old mapping at AMT: %d\n", this->amt[al_head->logical]);

        //Update AMT with with new mapping 
        this->amt[al_head->logical] = al_head->physical;
        //printf("commit() RMT and AMT should have same mapping now? \n"); //WIP:FIXME
        //printf("commit(): after PRF[%d]=%llu, PRF_READY[%d]=%llu\n",
        //        al_head->physical, this->prf[al_head->physical], al_head->physical, this->prf_ready[al_head->physical]);

        //printf("commit(): new mapping at AMT: %d\n", this->amt[al_head->logical]);

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

/*
    printf("commit(): AL head at (after): %d\n", this->al.head);
    printf("retired %d instructions so far\n", retired_insn++);
*/
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
    this->al.list[al.head]._retired = true;
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
/*
    printf("renamer::resolve() is called\n");
*/
    if (correct){ //branch was predicted correctly
        //clear the GBM bit by indexing with branch_ID
        this->GBM  &= ~(1ULL<<branch_ID);
        //clear all the checkpointed GBMs;
        int i;
        for (i=0; i < this->num_checkpoints; i++){
            this->checkpoints[i].gbm &= ~(1ULL<<branch_ID);
        } 
        //TODO: (verify) resetting the in_use to false
        this->checkpoints[branch_ID].__in_use = false; 

    } else {
        //FIXME: Initial implementation, double check
        // In the case of a misprediction:
        // * Restore the GBM from the branch's checkpoint. Also make sure the
        //   mispredicted branch's bit is cleared in the restored GBM,
        //   since it is now resolved and its bit and checkpoint are freed.
        // * You don't have to worry about explicitly freeing the GBM bits
        //   and checkpoints of branches that are after the mispredicted
        //   branch in program order. The mere act of restoring the GBM
        //   from the checkpoint achieves this feat.
        uint64_t misp_gbm = this->checkpoints[branch_ID].gbm;
        misp_gbm  &= ~(1ULL<<branch_ID);
        this->GBM = misp_gbm;

        // * Restore the RMT using the branch's checkpoint.
        int i;
        for (i=0; i < map_table_size; i++){
            this->rmt[i] = this->checkpoints[branch_ID].shadow_map_table[i]; 
        }
        // * Restore the Free List head pointer and its phase bit,
        //   using the branch's checkpoint.
        this->fl.head = this->checkpoints[branch_ID].free_list_head;
        this->fl.head_phase =this->checkpoints[branch_ID].free_list_head_phase;

        // * Restore the Active List tail pointer and its phase bit
        //   corresponding to the entry after the branch's entry.
        int recoverd_al_tail = AL_index + 1;
        this->al.tail = recoverd_al_tail;
        // AL cannot be empty, so it has to be partially full or completely
        // full.
        if (recoverd_al_tail == this->active_list_size){
            this->al.tail = 0;
        }

        //   Hints:
        //   You can infer the restored tail pointer from the branch's
        //   AL_index. You can infer the restored phase bit, using
        //   the phase bit of the Active List head pointer, where
        //   the restored Active List tail pointer is with respect to
        //   the Active List head pointer, and the knowledge that the
        //   Active List can't be empty at this moment (because the
        //   mispredicted branch is still in the Active List).
        // * Do NOT set the branch misprediction bit in the Active List.
        //   (Doing so would cause a second, full squash when the branch
        //   reaches the head of the Active List. We don’t want or need
        //   that because we immediately recover within this function.)

        // if hp == tp and h == t empty - not possible
        //    hp != tp and h == t full  - possible
        //    hp == tp     h  < t partly filled - possible
        //    hp != tp     h  > t possible 
        if (this->al.head == this->al.tail){
            this->al.tail_phase = !this->al.head_phase;
        }
        else if (this->al.head > this->al.tail){
            this->al.tail_phase = !this->al.head_phase;
        } else { // h < t, 
            this->al.tail_phase = this->al.head_phase;
        }


    }
}


void renamer::squash(){
    //FIXME: Not Implemented
    //What does squashing entail?
    //zero out all the instructions in AL. AL tail = head
/*
    printf("renamer::squash is called\n");
*/
    //the renamer should be rolled back to the committed state of the machine
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
/*
    printf("squash gets called. Not Implemented\n");
*/  
    return;
}

void renamer::set_exception(uint64_t AL_index){
    this->al.list[AL_index].exception = true;
}

void renamer::set_load_violation(uint64_t AL_index){
    this->al.list[AL_index].load_violation = true;
}

void renamer::set_branch_misprediction(uint64_t AL_index){
    this->al.list[AL_index].br_mispredict = true;
}

void renamer::set_value_misprediction(uint64_t AL_index){
    this->al.list[AL_index].val_mispredict = true;
}

bool renamer::get_exception(uint64_t AL_index){
    //TODO: throw exception if AL_index is invalid
    return this->al.list[AL_index].exception; 
}


int renamer::get_free_al_entry_count(){
    if (this->active_list_is_full()) return 0;
    if (this->active_list_is_empty()) return this->active_list_size;

    int used, free;
    if (this->al.head_phase == this->al.tail_phase){
        assert(this->al.tail > this->al.head);
        used = this->al.tail - this->al.head;
        free = this->active_list_size - used;
        return free;
    }
    else if (this->al.head_phase != this->al.tail_phase){
        assert(this->al.head > this->al.tail);
        free = this->al.head - this->al.tail; 
        return free;
    }

     // inconsistent state
    return -1;
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


void renamer::init_al_entry(al_entry_t *ale){
    //WIP: fix the values 
    //initiate all fields to 0 
    ale->has_dest=false;
    ale->logical=UINT64_MAX;
    ale->physical=UINT64_MAX;
    ale->completed=false;
    ale->exception=false;
    ale->load_violation=false;
    ale->br_mispredict=false;
    ale->val_mispredict=false;
    ale->is_load=false;
    ale->is_store=false;
    ale->is_branch=false;
    ale->is_amo=false;
    ale->is_csr=false;
    ale->pc=UINT64_MAX;
    ale->_retired=false;
}


//Debugging helper functions
void renamer::print_free_list(){
    uint64_t i=0;
    printf("--------------FREE LIST-------------------\n");
    while (i < free_list_size){
        if (i == fl.tail) printf("|%llu T(%d)", fl.list[i], fl.tail, fl.tail_phase);
        if (i == fl.head) printf("|%llu H(%d)", fl.list[i], fl.head, fl.head_phase);
        if (i != fl.head && i != fl.tail) printf("|%3llu ", fl.list[i]);
        i++;
    }
    printf("|\n");
    printf("------------END FREE LIST-----------------\n");
}
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
        printf("| %8llu ", prf[i]);
    }
    printf("\n-------------------END_PRF-----------------\n");

}
void renamer::print_prf_ready(){
    printf("---------------------PRF_READY-----------------\n");
    for (int i=0; i < this->num_phys_reg; i++){
        printf("| %llu ", prf_ready[i]);
    }
    printf("\n-------------------END_PRF_READY-----------------\n");

}
void renamer::print_active_list(bool between_head_and_tail){
    int i=0, n=active_list_size;
    if (between_head_and_tail) {
        i = this->al.head;
        n = this->al.tail;
    }
    if (n - i <= 0){
        i = 0;
        n = active_list_size;
        printf("ACTIVE LIST IS FULL. PRINTING ALL\n");
    }

    printf("| idx | log| phys | com | exc | dest | PC | _ret |\n");
    for (i; i < n; i++){
        al_entry_t *t;
        t = &this->al.list[i];
        printf("| %3d | %3d | %3d | %3d | %3d | %3d| %llu | %3d|\n",
                i,     t->logical, t->physical, t->completed,
                t->exception, t->has_dest, t->pc, t->_retired);
    }
    printf("AL Head: %d, AL tail: %d, Head Phase: %d, Tail Phase: %d\n",
            this->al.head, this->al.tail, this->al.head_phase, this->al.tail_phase
        );

}
