#include "n64system.h"

#include <string.h>
#include <unistd.h>

#include <mem/n64bus.h>
#include <frontend/render.h>
#include <interface/vi.h>
#include <interface/ai.h>
#include <mem/n64_rsp_bus.h>
#include <cpu/rsp.h>
#include <cpu/dynarec.h>
#include <sys/mman.h>
#include <errno.h>

bool should_quit = false;


n64_system_t* global_system;

#define selected_n64_system_step jit_system_step

// 128MiB codecache
#define CODECACHE_SIZE (1 << 27)
static byte codecache[CODECACHE_SIZE] __attribute__((aligned(4096)));

word read_rsp_word_wrapper(word address) {
    return n64_rsp_read_word(global_system, address);
}

void write_rsp_word_wrapper(word address, word value) {
    n64_rsp_write_word(global_system, address, value);
}

void write_physical_word_wrapper(word address, word value) {
    n64_write_word(global_system, address, value);
}

half read_rsp_half_wrapper(word address) {
    return n64_rsp_read_half(global_system, address);
}

void write_rsp_half_wrapper(word address, half value) {
    n64_rsp_write_half(global_system, address, value);
}

byte read_rsp_byte_wrapper(word address) {
    return n64_rsp_read_byte(global_system, address);
}

void write_rsp_byte_wrapper(word address, byte value) {
    n64_rsp_write_byte(global_system, address, value);
}

byte read_physical_byte_wrapper(word address) {
    return n64_read_byte(global_system, address);
}

void write_physical_byte_wrapper(word address, byte value) {
    n64_write_byte(global_system, address, value);
}

dword virtual_read_dword_wrapper(word address) {
    address = resolve_virtual_address(address, &global_system->cpu.cp0);
    return n64_read_dword(global_system, address);
}

void virtual_write_dword_wrapper(word address, dword value) {
    address = resolve_virtual_address(address, &global_system->cpu.cp0);
    n64_write_dword(global_system, address, value);
}

word virtual_read_word_wrapper(word address) {
    address = resolve_virtual_address(address, &global_system->cpu.cp0);
    return n64_read_physical_word(address);
}

void virtual_write_word_wrapper(word address, word value) {
    address = resolve_virtual_address(address, &global_system->cpu.cp0);
    n64_write_word(global_system, address, value);
}

half virtual_read_half_wrapper(word address) {
    address = resolve_virtual_address(address, &global_system->cpu.cp0);
    return n64_read_half(global_system, address);
}

void virtual_write_half_wrapper(word address, half value) {
    address = resolve_virtual_address(address, &global_system->cpu.cp0);
    n64_write_half(global_system, address, value);
}

byte virtual_read_byte_wrapper(word address) {
    address = resolve_virtual_address(address, &global_system->cpu.cp0);
    return n64_read_byte(global_system, address);
}

void virtual_write_byte_wrapper(word address, byte value) {
    address = resolve_virtual_address(address, &global_system->cpu.cp0);
    n64_write_byte(global_system, address, value);
}

n64_system_t* init_n64system(const char* rom_path, bool enable_frontend, bool enable_debug, n64_video_type_t video_type) {
    // align to page boundary
    n64_system_t* system;
    posix_memalign((void **) &system, sysconf(_SC_PAGESIZE), sizeof(n64_system_t));

    memset(system, 0x00, sizeof(n64_system_t));
    init_mem(&system->mem);
    if (rom_path != NULL) {
        load_n64rom(&system->mem.rom, rom_path);
    }

    system->video_type = video_type;

    system->cpu.branch = false;
    system->cpu.exception = false;

    system->cpu.read_dword = &virtual_read_dword_wrapper;
    system->cpu.write_dword = &virtual_write_dword_wrapper;

    system->cpu.read_word = &virtual_read_word_wrapper;
    system->cpu.write_word = &virtual_write_word_wrapper;

    system->cpu.read_half = &virtual_read_half_wrapper;
    system->cpu.write_half = &virtual_write_half_wrapper;

    system->cpu.read_byte = &virtual_read_byte_wrapper;
    system->cpu.write_byte = &virtual_write_byte_wrapper;

    //system->rsp.read_dword = &read_dword_wrapper;
    //system->rsp.write_dword = &write_dword_wrapper;

    system->rsp.read_word = &read_rsp_word_wrapper;
    system->rsp.write_word = &write_rsp_word_wrapper;

    system->rsp.read_half = &read_rsp_half_wrapper;
    system->rsp.write_half = &write_rsp_half_wrapper;

    system->rsp.read_byte = &read_rsp_byte_wrapper;
    system->rsp.write_byte = &write_rsp_byte_wrapper;

    system->rsp.read_physical_byte = &read_physical_byte_wrapper;
    system->rsp.write_physical_byte = &write_physical_byte_wrapper;

    system->rsp.read_physical_word = &n64_read_physical_word;
    system->rsp.write_physical_word = &write_physical_word_wrapper;

    for (int i = 0; i < SP_IMEM_SIZE / 4; i++) {
        system->rsp.icache[i].instruction.raw = 0;
        system->rsp.icache[i].handler = cache_rsp_instruction;
    }

    system->rsp.status.halt = true; // RSP starts halted

    system->vi.vi_v_intr = 256;

    system->dpc.status.raw = 0x80;


    system->ai.dac.frequency = 44100;
    system->ai.dac.precision = 16;
    system->ai.dac.period = CPU_HERTZ / system->ai.dac.frequency;

    system->si.controllers[0].plugged_in = true;
    system->si.controllers[1].plugged_in = false;
    system->si.controllers[2].plugged_in = false;
    system->si.controllers[3].plugged_in = false;

    if (mprotect(&codecache, CODECACHE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
        printf("Page size: %ld\n", sysconf(_SC_PAGESIZE));
        logfatal("mprotect codecache failed! %s", strerror(errno));
    }
    system->dynarec = n64_dynarec_init(system, codecache, CODECACHE_SIZE);

    global_system = system;
    if (enable_frontend) {
        render_init(system, video_type);
    }
    system->debugger_state.enabled = enable_debug;
    if (enable_debug) {
        debugger_init(system);
    }
    return system;
}

INLINE int jit_system_step(n64_system_t* system) {
    r4300i_t* cpu = &system->cpu;
    cpu->cp0.count += CYCLES_PER_INSTR;
    if (unlikely(cpu->cp0.count >> 1 == cpu->cp0.compare)) {
        cpu->cp0.cause.ip7 = true;
        loginfo("Compare interrupt!");
        r4300i_interrupt_update(cpu);
    }

    /* Commented out for now since the game never actually reads cp0.random
    if (cpu->cp0.random <= cpu->cp0.wired) {
        cpu->cp0.random = 31;
    } else {
        cpu->cp0.random--;
    }
     */

    if (unlikely(cpu->interrupts > 0)) {
        if(cpu->cp0.status.ie && !cpu->cp0.status.exl && !cpu->cp0.status.erl) {
            cpu->cp0.cause.interrupt_pending = cpu->interrupts;
            r4300i_handle_exception(cpu, cpu->pc, 0, cpu->interrupts);
            return CYCLES_PER_INSTR;
        }
    }
    static int cpu_steps = 0;
    int taken = n64_dynarec_step(system, system->dynarec);
    cpu_steps += taken;

    if (!system->rsp.status.halt) {
        // 2 RSP steps per 3 CPU steps
        system->rsp.steps += (cpu_steps / 3) * 2;
        cpu_steps -= cpu_steps % 3;

        rsp_run(system);
    }

    return taken;
}

INLINE int interpreter_system_step(n64_system_t* system) {
#ifdef N64_DEBUG_MODE
    if (system->debugger_state.enabled && check_breakpoint(&system->debugger_state, system->cpu.pc)) {
        debugger_breakpoint_hit(system);
    }
    while (system->debugger_state.broken) {
        usleep(1000);
        debugger_tick(system);
    }
#endif
    int taken = CYCLES_PER_INSTR;
    r4300i_step(&system->cpu);
    static int cpu_steps = 0;
    cpu_steps += taken;

    if (!system->rsp.status.halt) {
        // 2 RSP steps per 3 CPU steps
        system->rsp.steps += (cpu_steps / 3) * 2;
        cpu_steps -= cpu_steps % 3;

        rsp_run(system);
    }

    return taken;
}

// This is used for debugging tools, it's fine for now if timing is a little off.
void n64_system_step(n64_system_t* system) {
    r4300i_step(&system->cpu);
    if (!system->rsp.status.halt) {
        rsp_step(system);
    }
}

void check_vsync(n64_system_t* system) {
    if (system->vi.v_current == system->vi.vsync >> 1) {
        rdp_update_screen(system);
    }
}

void n64_system_loop(n64_system_t* system) {
    int cycles = 0;
    while (!should_quit) {
        for (system->vi.v_current = 0; system->vi.v_current < NUM_SHORTLINES; system->vi.v_current++) {
            check_vi_interrupt(system);
            check_vsync(system);
            while (cycles <= SHORTLINE_CYCLES) {
                cycles += selected_n64_system_step(system);
                system->debugger_state.steps = 0;
            }
            cycles -= SHORTLINE_CYCLES;
            ai_step(system, SHORTLINE_CYCLES);
        }
        for (; system->vi.v_current < NUM_SHORTLINES + NUM_LONGLINES; system->vi.v_current++) {
            check_vi_interrupt(system);
            check_vsync(system);
            while (cycles <= LONGLINE_CYCLES) {
                cycles += selected_n64_system_step(system);
                system->debugger_state.steps = 0;
            }
            cycles -= LONGLINE_CYCLES;
            ai_step(system, LONGLINE_CYCLES);
        }
        check_vi_interrupt(system);
        check_vsync(system);
#ifdef N64_DEBUG_MODE
        if (system->debugger_state.enabled) {
            debugger_tick(system);
        }
#endif
    }
}

void n64_system_cleanup(n64_system_t* system) {
    rdp_cleanup();
    debugger_cleanup(system);

    free(system->mem.rom.rom);
    system->mem.rom.rom = NULL;

    free(system->mem.rom.pif_rom);
    system->mem.rom.pif_rom = NULL;

    free(system);
}

void n64_request_quit() {
    should_quit = true;
}

void on_interrupt_change(n64_system_t* system) {
    bool interrupt = system->mi.intr.raw & system->mi.intr_mask.raw;
    loginfo("ip2 is now: %d", interrupt);
    system->cpu.cp0.cause.ip2 = interrupt;
    r4300i_interrupt_update(&system->cpu);
}

void interrupt_raise(n64_interrupt_t interrupt) {
    switch (interrupt) {
        case INTERRUPT_VI:
            loginfo("Raising VI interrupt");
            global_system->mi.intr.vi = true;
            break;
        case INTERRUPT_SI:
            loginfo("Raising SI interrupt");
            global_system->mi.intr.si = true;
            break;
        case INTERRUPT_PI:
            loginfo("Raising PI interrupt");
            global_system->mi.intr.pi = true;
            break;
        case INTERRUPT_AI:
            loginfo("Raising AI interrupt");
            global_system->mi.intr.ai = true;
            break;
        case INTERRUPT_DP:
            loginfo("Raising DP interrupt");
            global_system->mi.intr.dp = true;
            break;
        case INTERRUPT_SP:
            loginfo("Raising SP interrupt");
            global_system->mi.intr.sp = true;
            break;
        default:
            logfatal("Raising unimplemented interrupt: %d", interrupt);
    }

    on_interrupt_change(global_system);
}

void interrupt_lower(n64_system_t* system, n64_interrupt_t interrupt) {
    switch (interrupt) {
        case INTERRUPT_VI:
            system->mi.intr.vi = false;
            loginfo("Lowering VI interrupt");
            break;
        case INTERRUPT_SI:
            system->mi.intr.si = false;
            loginfo("Lowering SI interrupt");
            break;
        case INTERRUPT_PI:
            system->mi.intr.pi = false;
            loginfo("Lowering PI interrupt");
            break;
        case INTERRUPT_DP:
            system->mi.intr.dp = false;
            loginfo("Lowering DP interrupt");
            break;
        case INTERRUPT_AI:
            system->mi.intr.ai = false;
            loginfo("Lowering DP interrupt");
            break;
        case INTERRUPT_SP:
            system->mi.intr.sp = false;
            loginfo("Lowering SP interrupt");
            break;
        default:
            logfatal("Lowering unimplemented interrupt: %d", interrupt);
    }

    on_interrupt_change(system);
}
