#include "dynarec.h"

#include <mem/n64bus.h>
#include <dynasm/dasm_proto.h>
#include "cpu/dynarec/asm_emitter.h"
#include "dynarec_memory_management.h"

#define IS_PAGE_BOUNDARY(address) ((address & (BLOCKCACHE_PAGE_SIZE - 1)) == 0)

void* link_and_encode(n64_dynarec_t* dynarec, dasm_State** d) {
    size_t code_size;
    dasm_link(d, &code_size);
#ifdef N64_LOG_COMPILATIONS
    printf("Generated %ld bytes of code\n", code_size);
#endif
    void* buf = dynarec_bumpalloc(dynarec, code_size);
    dasm_encode(d, buf);

    return buf;
}


void compile_new_block(n64_dynarec_t* dynarec, r4300i_t* compile_time_cpu, n64_dynarec_block_t* block, dword virtual_address, word physical_address) {
    dasm_State* d = block_header();
    dasm_State** Dst = &d;

    bool should_continue_block = true;
    int block_length = 0;


    int instructions_left_in_block = -1;

    dynarec_instruction_category_t prev_instr_category = NORMAL;

    do {
        mips_instruction_t instr;
        instr.raw = n64_read_physical_word(physical_address);

        word next_physical_address = physical_address + 4;
        dword next_virtual_address = virtual_address + 4;

        advance_pc(compile_time_cpu, Dst);

        instructions_left_in_block--;
        bool instr_ends_block;

        word extra_cycles = 0;
        dynarec_ir_t* ir = instruction_ir(instr, physical_address);
        if (ir->exception_possible) {
            // save prev_pc
            // TODO will no longer need this when we emit code to check the exceptions
            flush_prev_pc(Dst, virtual_address);
        }
        ir->compiler(Dst, instr, physical_address, &extra_cycles);
        block_length++;
        block_length += extra_cycles;
        if (ir->exception_possible) {
            check_exception(Dst, block_length);
        }

        switch (ir->category) {
            case NORMAL:
                instr_ends_block = instructions_left_in_block == 0;
                break;
            case BRANCH:
                if (prev_instr_category == BRANCH || prev_instr_category == BRANCH_LIKELY) {
                    // Check if the previous branch was taken.

                    // If the last branch wasn't taken, we can treat this the same as if the previous instruction wasn't a branch
                    // just set the cpu->last_branch_taken to cpu->branch_taken and execute the next instruction.

                    // emit:
                    // if (!cpu->last_branch_taken) cpu->last_branch_taken = cpu->branch_taken;
                    logfatal("Branch in a branch delay slot");
                } else {
                    // If the last instruction wasn't a branch, no special behavior is needed. Just set up some state in case the next one is.
                    // emit:
                    // cpu->last_branch_taken = cpu->branch_taken;
                    //logfatal("unimp");
                }

                // If the previous instruction was a branch, exit the block early if

                // If the previous instruction was a branch LIKELY, exit the block early if the _previous branch_
                instr_ends_block = false;
                instructions_left_in_block = 1; // emit delay slot
                break;

            case BRANCH_LIKELY:
                // If the previous instruction was a branch:
                //
                if (prev_instr_category == BRANCH || prev_instr_category == BRANCH_LIKELY) {
                    logfatal("Branch in a branch likely delay slot");
                } else {
                    end_block_early_on_branch_taken(Dst, block_length);
                }

                instr_ends_block = false;
                instructions_left_in_block = 1; // emit delay slot
                break;
            case ERET:
            case TLB_WRITE:
            case STORE:
                instr_ends_block = true;
                break;

            default:
                logfatal("Unknown dynarec instruction type");
        }

        bool page_boundary_ends_block = IS_PAGE_BOUNDARY(next_physical_address);
        // !!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!
        // If the first instruction in the new page is a delay slot, INCLUDE IT IN THE BLOCK ANYWAY.
        // This DOES BREAK a corner case!
        // If the game overwrites the delay slot but does not overwrite the branch or anything in the other page,
        // THIS BLOCK WILL NOT GET MARKED DIRTY.
        // I highly doubt any games do it, but THIS NEEDS TO GET FIXED AT SOME POINT
        // !!!!!!!!!!!!!!! WARNING !!!!!!!!!!!!!!!
        if (instructions_left_in_block == 1) { page_boundary_ends_block = false; } // FIXME, TODO, BAD, EVIL, etc

        if (instr_ends_block || page_boundary_ends_block) {
#ifdef N64_LOG_COMPILATIONS
            printf("Ending block. instr: %d pb: %d (0x%08X)\n", instr_ends_block, page_boundary_ends_block, next_physical_address);
#endif
            should_continue_block = false;
        }

        physical_address = next_physical_address;
        virtual_address = next_virtual_address;
        prev_instr_category = ir->category;
    } while (should_continue_block);
    end_block(Dst, block_length);
    void* compiled = link_and_encode(dynarec, &d);
    dasm_free(&d);

    block->run = compiled;
}


int missing_block_handler(r4300i_t* cpu) {
    word physical = resolve_virtual_address(cpu->pc, &cpu->cp0);
    word outer_index = physical >> BLOCKCACHE_OUTER_SHIFT;
    // TODO: put the dynarec object inside the r4300i_t object to get rid of this need for global_system
    n64_dynarec_block_t* block_list = global_system->dynarec->blockcache[outer_index];
    word inner_index = (physical & (BLOCKCACHE_PAGE_SIZE - 1)) >> 2;

    n64_dynarec_block_t* block = &block_list[inner_index];

#ifdef N64_LOG_COMPILATIONS
    printf("Compilin' new block at 0x%08X / 0x%08X\n", global_system->cpu.pc, physical);
#endif

    compile_new_block(global_system->dynarec, cpu, block, cpu->pc, physical);

    return block->run(cpu);
}

int n64_dynarec_step(n64_system_t* system, n64_dynarec_t* dynarec) {
    word physical = resolve_virtual_address(system->cpu.pc, &system->cpu.cp0);
    word outer_index = physical >> BLOCKCACHE_OUTER_SHIFT;
    n64_dynarec_block_t* block_list = dynarec->blockcache[outer_index];
    word inner_index = (physical & (BLOCKCACHE_PAGE_SIZE - 1)) >> 2;

    if (unlikely(block_list == NULL)) {
#ifdef N64_LOG_COMPILATIONS
        printf("Need a new block list for page 0x%05X (address 0x%08X virtual 0x%08X)\n", outer_index, physical, system->cpu.pc);
#endif
        block_list = dynarec_bumpalloc_zero(dynarec, BLOCKCACHE_INNER_SIZE * sizeof(n64_dynarec_block_t));
        for (int i = 0; i < BLOCKCACHE_INNER_SIZE; i++) {
            block_list[i].run = missing_block_handler;
        }
        dynarec->blockcache[outer_index] = block_list;
    }

    n64_dynarec_block_t* block = &block_list[inner_index];

#ifdef LOG_ENABLED
    static long total_blocks_run;
    logdebug("Running block at 0x%016lX - block run #%ld - block FP: 0x%016lX", system->cpu.pc, ++total_blocks_run, (uintptr_t)block->run);
#endif
    int taken = block->run(&system->cpu);
#ifdef N64_LOG_JIT_SYNC_POINTS
    printf("JITSYNC %d %08X ", taken, system->cpu.pc);
    for (int i = 0; i < 32; i++) {
        printf("%016lX", system->cpu.gpr[i]);
        if (i != 31) {
            printf(" ");
        }
    }
    printf("\n");
#endif
    logdebug("Done running block - took %d cycles - pc is now 0x%016lX", taken, system->cpu.pc);

    return taken * CYCLES_PER_INSTR;
}

n64_dynarec_t* n64_dynarec_init(n64_system_t* system, byte* codecache, size_t codecache_size) {
#ifdef N64_LOG_COMPILATIONS
    printf("Trying to malloc %ld bytes\n", sizeof(n64_dynarec_t));
#endif
    n64_dynarec_t* dynarec = calloc(1, sizeof(n64_dynarec_t));

    dynarec->codecache_size = codecache_size;
    dynarec->codecache_used = 0;

    for (int i = 0; i < BLOCKCACHE_OUTER_SIZE; i++) {
        dynarec->blockcache[i] = NULL;
    }

    dynarec->codecache = codecache;
    return dynarec;
}

void invalidate_dynarec_page(n64_dynarec_t* dynarec, word physical_address) {
    word outer_index = physical_address >> BLOCKCACHE_OUTER_SHIFT;
    dynarec->blockcache[outer_index] = NULL;
}