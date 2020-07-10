#include "rsp_instructions.h"
#include "../common/log.h"

#define RSP_REG_LR 31

void rsp_branch_abs(rsp_t* rsp, word address) {
    rsp->branch_pc = address;

    // Execute one instruction before taking the branch_offset
    rsp->branch = true;
    rsp->branch_delay = 1;

    printf("Setting up a branch_offset (delayed by 1 instruction) to 0x%08X\n", rsp->branch_pc);
}

void rsp_branch_offset(rsp_t* rsp, shalf offset) {
    sword soffset = offset;
    soffset <<= 2;
    // This is taking advantage of the fact that we add 4 to the PC after each instruction.
    // Due to the compiler expecting pipelining, the address we get here will be 4 _too early_

    rsp_branch_abs(rsp, rsp->pc + soffset);
}

void rsp_conditional_branch(rsp_t* rsp, word offset, bool condition) {
    if (condition) {
        rsp_branch_offset(rsp, offset);
    }
}

INLINE void rsp_link(rsp_t* rsp) {
    set_rsp_register(rsp, RSP_REG_LR, rsp->pc + 4); // Skips the instruction in the delay slot on return
}

RSP_INSTR(rsp_ori) {
    set_rsp_register(rsp, instruction.i.rt, get_rsp_register(rsp, instruction.i.rs) | instruction.i.immediate);
}

RSP_INSTR(rsp_addi) {
    sword reg_addend = get_rsp_register(rsp, instruction.i.rs);
    shalf imm_addend = instruction.i.immediate;
    sword result = imm_addend + reg_addend;
    set_rsp_register(rsp, instruction.i.rt, result);
}

RSP_INSTR(rsp_spc_sll) {
    word value = get_rsp_register(rsp, instruction.r.rt);
    word result = value << instruction.r.sa;
    set_rsp_register(rsp, instruction.r.rd, result);
}

RSP_INSTR(rsp_spc_srl) {
        word value = get_rsp_register(rsp, instruction.r.rt);
        word result = value >> instruction.r.sa;
        set_rsp_register(rsp, instruction.r.rd, result);
}

RSP_INSTR(rsp_spc_add) {
    word addend1 = get_rsp_register(rsp, instruction.r.rs);
    word addend2 = get_rsp_register(rsp, instruction.r.rt);

    word result = addend1 + addend2;

    set_rsp_register(rsp, instruction.r.rd, result);
}

void rsp_spc_break(n64_system_t* system, mips_instruction_t instruction) {
    system->rsp.status.halt = true;
    system->rsp.status.broke = true;

    if (system->rsp.status.intr_on_break) {
        interrupt_raise(system, INTERRUPT_SP);
    }
}

RSP_INSTR(rsp_andi) {
        word immediate = instruction.i.immediate;
        word result = immediate & get_rsp_register(rsp, instruction.i.rs);
        set_rsp_register(rsp, instruction.i.rt, result);
}

RSP_INSTR(rsp_sb) {
    shalf offset = instruction.i.immediate;
    word address = get_rsp_register(rsp, instruction.i.rs) + offset;

    byte value = get_rsp_register(rsp, instruction.i.rt);
    rsp->write_byte(address, value);
}

RSP_INSTR(rsp_sh) {
    shalf offset = instruction.i.immediate;
    word address = get_rsp_register(rsp, instruction.i.rs) + offset;
    if ((address & 0b1) > 0) {
        logfatal("TODO: is the RSP allowed to write to unaligned addresses?")
    }

    half value = get_rsp_register(rsp, instruction.i.rt);
    rsp->write_half(address, value);
}

RSP_INSTR(rsp_sw) {
    shalf offset = instruction.i.immediate;
    word address = get_rsp_register(rsp, instruction.i.rs) + offset;
    if ((address & 0b11) > 0) {
        logfatal("TODO: is the RSP allowed to write to unaligned addresses?")
    }

    word value = get_rsp_register(rsp, instruction.i.rt);
    rsp->write_word(address, value);
}

RSP_INSTR(rsp_lhu) {
    shalf offset = instruction.i.immediate;
    word address = get_rsp_register(rsp, instruction.i.rs) + offset;
    if ((address & 0b1) > 0) {
        logfatal("TODO: RSP is allowed to read from unaligned addresses, but what comes back?")
    }

    half value = rsp->read_half(address);
    set_rsp_register(rsp, instruction.i.rt, value);
}

RSP_INSTR(rsp_lh) {
    shalf offset = instruction.i.immediate;
    word address = get_rsp_register(rsp, instruction.i.rs) + offset;
    if ((address & 0b1) > 0) {
        logfatal("TODO: RSP is allowed to read from unaligned addresses, but what comes back?")
    }

    shalf value = rsp->read_half(address);
    set_rsp_register(rsp, instruction.i.rt, (sword)value);
}

RSP_INSTR(rsp_lw) {
    shalf offset = instruction.i.immediate;
    word address = get_rsp_register(rsp, instruction.i.rs) + offset;
    if ((address & 0b11) > 0) {
        logfatal("TODO: RSP is allowed to read from unaligned addresses, but what comes back?")
    }

    sword value = rsp->read_word(address);
    set_rsp_register(rsp, instruction.i.rt, value);
}

RSP_INSTR(rsp_j) {
    word target = instruction.j.target;
    target <<= 2;
    target |= ((rsp->pc - 4) & 0xF0000000); // PC is 4 ahead

    rsp_branch_abs(rsp, target);
}

RSP_INSTR(rsp_jal) {
    rsp_link(rsp);
    word target = instruction.j.target;
    target <<= 2;
    target |= ((rsp->pc - 4) & 0xF0000000); // PC is 4 ahead

    rsp_branch_abs(rsp, target);
}

RSP_INSTR(rsp_spc_jr) {
    rsp_branch_abs(rsp, get_rsp_register(rsp, instruction.r.rs));
}

void rsp_mfc0(n64_system_t* system, mips_instruction_t instruction) {
    sword value = get_rsp_cp0_register(system, instruction.r.rd);
    set_rsp_register(&system->rsp, instruction.r.rt, (sdword)value);
}

void rsp_mtc0(n64_system_t* system, mips_instruction_t instruction) {
    word value = get_rsp_register(&system->rsp, instruction.r.rt);
    set_rsp_cp0_register(system, instruction.r.rd, value);
}

RSP_INSTR(rsp_bne) {
    rsp_conditional_branch(rsp, instruction.i.immediate, get_rsp_register(rsp, instruction.i.rs) != get_rsp_register(rsp, instruction.i.rt));
}

RSP_INSTR(rsp_beq) {
    rsp_conditional_branch(rsp, instruction.i.immediate, get_rsp_register(rsp, instruction.i.rs) == get_rsp_register(rsp, instruction.i.rt));
}

RSP_INSTR(rsp_bgtz) {
    sword reg = get_rsp_register(rsp, instruction.i.rs);
    rsp_conditional_branch(rsp, instruction.i.immediate, reg > 0);
}

RSP_INSTR(rsp_blez) {
    sword reg = get_rsp_register(rsp, instruction.i.rs);
    rsp_conditional_branch(rsp, instruction.i.immediate, reg <= 0);
}