#ifndef N64_N64SYSTEM_H
#define N64_N64SYSTEM_H
#include <stdbool.h>
#include "../mem/n64mem.h"
#include "../cpu/r4300i.h"
#include "../cpu/rsp_status.h"
#include "../vi_reg.h"

typedef enum n64_interrupt {
    INTERRUPT_VI,
    INTERRUPT_SI,
    INTERRUPT_PI,
    INTERRUPT_DP,
    INTERRUPT_AI,
} n64_interrupt_t;

typedef union mi_intr_mask {
    word raw;
    struct {
        bool sp:1;
        bool si:1;
        bool ai:1;
        bool vi:1;
        bool pi:1;
        bool dp:1;
        unsigned:26;
    };
} mi_intr_mask_t;

typedef union mi_intr {
    word raw;
    struct {
        bool sp:1;
        bool si:1;
        bool ai:1;
        bool vi:1;
        bool pi:1;
        bool dp:1;
        unsigned:26;
    };
} mi_intr_t;

typedef struct n64_controller {
    union {
        byte byte1;
        struct {
            bool a:1;
            bool b:1;
            bool z:1;
            bool start:1;
            bool dp_up:1;
            bool dp_down:1;
            bool dp_left:1;
            bool dp_right:1;
        };
    };
    union {
        byte byte2;
        struct {
            bool joy_reset:1;
            bool zero:1;
            bool l:1;
            bool r:1;
            bool c_up:1;
            bool c_down:1;
            bool c_left:1;
            bool c_right:1;
        };
    };
    sbyte joy_x;
    sbyte joy_y;
} n64_controller_t;

typedef struct n64_system {
    n64_mem_t mem;
    r4300i_t cpu;
    r4300i_t rsp;
    rsp_status_t rsp_status;
    struct {
        word init_mode;
        mi_intr_mask_t intr_mask;
        mi_intr_t intr;
    } mi;
    struct {
        vi_status_t status;
        word vi_origin;
        word vi_width;
        word vi_v_intr;
        vi_burst_t vi_burst;
        word vsync;
        word hsync;
        word leap;
        word hstart;
        union {
            word raw;
            struct {
                unsigned vend:10;
                unsigned:5;
                unsigned vstart:10;
                unsigned:6;
            };
        } vstart;
        word vburst;
        word xscale;
        word yscale;
        word v_current;
        int calculated_height;
    } vi;
    struct {
        bool dma_enable;
        half dac_rate;
        byte bitrate;
        int dma_count;
        word dma_length[2];
        word dma_address[2];
        int cycles;

        struct {
            word frequency;
            word period;
            word precision;
        } dac;
    } ai;
    struct {
        n64_controller_t controllers[4];
    } si;
} n64_system_t;

n64_system_t* init_n64system(const char* rom_path, bool enable_frontend);

void n64_system_step(n64_system_t* system);
void n64_system_loop(n64_system_t* system);
void n64_system_cleanup(n64_system_t* system);
void n64_request_quit();
void interrupt_raise(n64_system_t* system, n64_interrupt_t interrupt);
void interrupt_lower(n64_system_t* system, n64_interrupt_t interrupt);
#endif //N64_N64SYSTEM_H
