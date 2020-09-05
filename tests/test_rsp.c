#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <bzlib.h>
#include <system/n64system.h>
#include <cpu/rsp.h>

// Just to make sure we don't get caught in an infinite loop
#define MAX_CYCLES 100000

// This is a little brittle, but it's fine. The logs shouldn't ever change.
// 1467 characters plus a newline
#define LINE_LENGTH 1468

void load_rsp_imem(n64_system_t* system, const char* rsp_path) {
    FILE* rsp = fopen(rsp_path, "rb");
    // This file is already in big endian
    size_t read = fread(system->mem.sp_imem, 1, SP_IMEM_SIZE, rsp);
    if (read == 0) {
        logfatal("Read 0 bytes from %s", rsp_path)
    }
}

void load_rsp_dmem(n64_system_t* system, word* input, int input_size) {
    for (int i = 0; i < input_size; i++) {
        // This translates to big endian
        word address = i * 4;
        system->rsp.write_word(address, input[i]);
    }
}

void compare_128(char* name, vu_reg_t reg, char* tok) {
    bool correct[16];
    bool all_correct = true;
    for (int byte_index = 0; byte_index < 16; byte_index++) {
        byte actual = reg.bytes[15 - byte_index];
        char expected_ascii[3];

        expected_ascii[0] = tok[byte_index * 2];
        expected_ascii[1] = tok[byte_index * 2 + 1];
        expected_ascii[2] = '\0';

        byte expected = strtol(expected_ascii, NULL, 16);

        correct[byte_index] = expected == actual;
        all_correct &= correct[byte_index];

        if (expected != actual) {
            printf("%s byte index %d: Expected: %02X actual: %02X\n", name, byte_index, expected, actual);
        }
    }

    if (!all_correct) {
        printf("Expected: %s\n", tok);
        printf("Actual:   ");
        for (int i = 0; i < 16; i++) {
            if (!correct[i]) {
                printf(COLOR_RED);
            }
            printf("%02X", reg.bytes[15 - i]);
            if (!correct[i]) {
                printf(COLOR_END);
            }
        }
        printf("\n");
        logfatal("Log mismatch!")
    }
}

void compare_16(char* name, half actual, half expected) {
    if (actual != expected) {
        printf("%s expected: 0x%04X\n", name, expected);
        printf("%s actual:   0x%04X\n", name, actual);
        logfatal("Log mismatch")
    }
}

void compare_8(char* name, byte actual, byte expected) {
    if (actual != expected) {
        printf("%s expected: 0x%02X\n", name, expected);
        printf("%s actual:   0x%02X\n", name, actual);
        logfatal("Log mismatch")
    }
}

bool run_test(n64_system_t* system, word* input, int input_size, word* output, int output_size, BZFILE* log_file) {
    load_rsp_dmem(system, input, input_size / 4);

    system->rsp.status.halt = false;
    system->rsp.pc = 0;

    int cycles = 0;

    while (!system->rsp.status.halt) {
        if (cycles >= MAX_CYCLES) {
            logfatal("Test ran too long and was killed! Possible infinite loop?")
        }
        char log_line[LINE_LENGTH];
        int error;
        BZ2_bzRead(&error, log_file, log_line, LINE_LENGTH - 1); // Don't read the newline char (read it separately so the last line doesn't fail)

        if (error == BZ_OK) {
            char* tok = strtok(log_line, " ");
            for (int vu_reg = 0; vu_reg < 32; vu_reg++) {
                char namebuf[5];
                snprintf(namebuf, 5, "vu%d", vu_reg);
                compare_128(namebuf, system->rsp.vu_regs[vu_reg], tok);
                tok = strtok(NULL, " ");
            }

            compare_128("ACC_L", system->rsp.acc.l, tok);
            tok = strtok(NULL, " ");
            compare_128("ACC_M", system->rsp.acc.m, tok);
            tok = strtok(NULL, " ");
            compare_128("ACC_H", system->rsp.acc.h, tok);
            tok = strtok(NULL, " ");

            half expected_vco = strtol(tok, NULL, 16);
            compare_16("VCO", rsp_get_vco(&system->rsp), expected_vco);
            tok = strtok(NULL, " ");

            byte expected_vce = strtol(tok, NULL, 16);
            compare_8("VCE", rsp_get_vce(&system->rsp), expected_vce);
            tok = strtok(NULL, " ");

            half expected_vcc = strtol(tok, NULL, 16);
            compare_16("VCC", rsp_get_vcc(&system->rsp), expected_vcc);
            tok = strtok(NULL, " ");

            bool expected_divin_loaded = strcmp(tok, "1") == 0;
            if (expected_divin_loaded != system->rsp.divin_loaded) {
                printf("divin_loaded expected: %d\n", expected_divin_loaded);
                printf("divin_loaded actual:   %d\n", system->rsp.divin_loaded);
                logfatal("Log mismatch!")
            }
            tok = strtok(NULL, " ");

            // Only check if divin_loaded is true
            half expected_divin = strtol(tok, NULL, 16);
            if (expected_divin_loaded) {
                compare_16("divin", system->rsp.divin, expected_divin);
            }
            tok = strtok(NULL, " ");

            half expected_divout = strtol(tok, NULL, 16);
            compare_16("divout", system->rsp.divout, expected_divout);
            tok = strtok(NULL, " ");

            for (int r = 0; r < 32; r++) {
                word expected = strtol(tok, NULL, 16);
                word actual = system->rsp.gpr[r];

                if (expected != actual) {
                    printf("r%d expected: 0x%08X\n", r, expected);
                    printf("r%d actual:   0x%08X\n", r, actual);
                    logfatal("Log mismatch!")
                }

                tok = strtok(NULL, " ");
            }
            BZ2_bzRead(&error, log_file, log_line, 1); // Read the newline char
        } else if (error == BZ_STREAM_END) {
            logwarn("Reached end of log file, continuing without checking the log!")
        } else {
            logfatal("Failed to read log line! Error: %d (check bzlib.h)", error)
        }

        cycles++;
        rsp_step(system);
    }

    bool failed = false;
    printf("\n\n================= Expected =================    ================== Actual ==================\n");
    printf("          0 1 2 3  4 5 6 7  8 9 A B  C D E F              0 1 2 3  4 5 6 7  8 9 A B  C D E F\n");
    for (int i = 0; i < output_size; i += 16) {
        printf("0x%04X:  ", 0x800 + i);

        for (int b = 0; b < 16; b++) {
            if (b != 0 && b % 4 == 0) {
                printf(" ");
            }
            if (i + b < output_size) {
                printf("%02X", ((byte*)output)[i + b]);
            } else {
                printf("  ");
            }
        }

        printf("    0x%04X:  ", 0x800 + i);

        for (int b = 0; b < 16; b++) {
            if (b != 0 && b % 4 == 0) {
                printf(" ");
            }
            if (i + b < output_size) {
                byte actual = system->mem.sp_dmem[0x800 + i + b];
                byte expected = ((byte*)output)[i + b];

                if (actual != expected) {
                    printf(COLOR_RED);
                    failed = true;
                }
                printf("%02X", actual);
                if (actual != expected) {
                    printf(COLOR_END);
                }
            } else {
                printf("  ");
            }
        }

        printf("\n");
    }
    printf("          0 1 2 3  4 5 6 7  8 9 A B  C D E F              0 1 2 3  4 5 6 7  8 9 A B  C D E F\n");

    printf("\n\n");

    return failed;
}

n64_system_t* load_test(const char* rsp_path) {
    n64_system_t* system = init_n64system(NULL, false, false);
    load_rsp_imem(system, rsp_path);
    return system;
}

int main(int argc, char** argv) {
    if (argc < 5) {
        logfatal("Not enough arguments")
    }

    log_set_verbosity(LOG_VERBOSITY_DEBUG)

    const char* test_name = argv[1];
    int input_size = atoi(argv[2]);
    int output_size = atoi(argv[3]);

    if (input_size % 4 != 0) {
        logfatal("Invalid input size: %d is not divisible by 4.", input_size)
    }

    if (output_size % 4 != 0) {
        logfatal("Invalid output size: %d is not divisible by 4.", output_size)
    }


    char input_data_path[PATH_MAX];
    snprintf(input_data_path, PATH_MAX, "%s.input", test_name);
    FILE* input_data_handle = fopen(input_data_path, "rb");

    char output_data_path[PATH_MAX];
    snprintf(output_data_path, PATH_MAX, "%s.golden", test_name);
    FILE* output_data_handle = fopen(output_data_path, "rb");


    bool failed = false;

    char rsp_path[PATH_MAX];
    snprintf(rsp_path, PATH_MAX, "%s.rsp", test_name);

    n64_system_t* system = load_test(rsp_path);
    unimplemented(system == NULL, "asdf")

    for (int i = 4; i < argc; i++) {
        const char* subtest_name = argv[i];
        byte input[input_size];
        fread(input, 1, input_size, input_data_handle);
        byte output[output_size];
        fread(output, 1, output_size, output_data_handle);

        char log_path[PATH_MAX];
        snprintf(log_path, PATH_MAX, "%s.%s.log.bz2", test_name, subtest_name);
        printf("Loading log from %s\n", log_path);
        BZFILE* log_file = BZ2_bzopen(log_path, "r");

        bool subtest_failed = run_test(system, (word *) input, input_size, (word *) output, output_size, log_file);

        BZ2_bzclose(log_file);

        if (subtest_failed) {
            printf("[%s %s] FAILED\n", test_name, subtest_name);
        } else {
            printf("[%s %s] PASSED\n", test_name, subtest_name);
        }

        failed |= subtest_failed;
        if (failed) {
            break;
        }
    }

    free(system);
    exit(failed);
}