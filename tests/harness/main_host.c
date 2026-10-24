/* Harness entry point: load the game the same way src/main.c does on the
 * Vita and run it on this thread.
 *
 *   harness <data_dir>      (data_dir = game_out/, holding the .s3e.raw)
 */
#define _GNU_SOURCE
#include "loader/s3e_loader.h"
#include "platform/log.h"
#include "platform/platform.h"
#include "s3e/s3e_internal.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>

static uintptr_t s_stack_top;

static void on_fault(int sig, siginfo_t *si, void *uc_)
{
    ucontext_t *uc = uc_;
    unsigned long pc = uc->uc_mcontext.arm_pc, lr = uc->uc_mcontext.arm_lr;
    int in_game = pc >= sf_game.base && pc < sf_game.base + sf_game.text_size + sf_game.data_size;
    LOGE("SIGNAL %d addr=%p pc=%#lx%s lr=%#lx sp=%#lx", sig, si->si_addr, pc,
         in_game ? " (game)" : "", lr, (unsigned long)uc->uc_mcontext.arm_sp);
    if (sf_game.base) {
        LOGE("  game link addr pc=%#x lr=%#x", (unsigned)sf_s3e_to_link(&sf_game, pc),
             (unsigned)sf_s3e_to_link(&sf_game, lr));
    }
    LOGE("  r0=%08lx r1=%08lx r2=%08lx r3=%08lx r4=%08lx r5=%08lx r6=%08lx r7=%08lx",
         uc->uc_mcontext.arm_r0, uc->uc_mcontext.arm_r1, uc->uc_mcontext.arm_r2,
         uc->uc_mcontext.arm_r3, uc->uc_mcontext.arm_r4, uc->uc_mcontext.arm_r5,
         uc->uc_mcontext.arm_r6, uc->uc_mcontext.arm_r7);
    /* Return addresses into the game on the stack: a cheap backtrace that
     * also shows the shape of a recursion. Printed as link addresses. */
    if (sf_game.base) {
        /* Walk the whole stack (up to the top of the initial stack mapping)
         * and print the first and last game return addresses. */
        /* On an overflow sp sits in the guard page: start from the next page. */
        uintptr_t lo = uc->uc_mcontext.arm_sp;
        const uint32_t *sp;
        const uint32_t *top = (const uint32_t *)s_stack_top;
        long i, n = 0, total = 0;
        if ((uintptr_t)si->si_addr >= lo - 64 && (uintptr_t)si->si_addr < lo + 4096)
            lo = ((uintptr_t)si->si_addr + 4096) & ~(uintptr_t)4095;
        sp = (const uint32_t *)lo;
        for (i = 0; sp + i < top; i++) {
            uint32_t v = sp[i];
            if (v >= sf_game.base && v < sf_game.base + sf_game.text_size) {
                total++;
                if (n < 12 || sp + i > top - 2048) {
                    LOGE("  stack[+%#lx] -> %#x", i * 4, (unsigned)sf_s3e_to_link(&sf_game, v));
                    n++;
                }
            }
        }
        LOGE("  %ld game return addresses on the stack", total);
    }
    sf_log_flush();
    _exit(128 + sig);
}

int main(int argc, char **argv)
{
    struct sigaction sa;
    char path[300];
    /* The Vita maps the image at 0x98000000, not its link base, so load
     * there by default: a run at the link base never applies a relocation.
     * SF_LOAD_ADDR=0x4a000000 reproduces the old zero-delta layout. */
    const char *env_addr = getenv("SF_LOAD_ADDR");
    uintptr_t load_addr = env_addr ? (uintptr_t)strtoul(env_addr, NULL, 0) : 0x98000000;

    s_stack_top = (uintptr_t)&sa;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <data_dir>\n", argv[0]);
        return 2;
    }
    snprintf(sf_data_dir, sizeof(sf_data_dir), "%s", argv[1]);
    sf_log_init();

    /* Handle faults on their own stack so a stack overflow is reported. */
    {
        static char altstack[256 * 1024];
        stack_t ss = { .ss_sp = altstack, .ss_size = sizeof(altstack) };
        sigaltstack(&ss, NULL);
    }
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = on_fault;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGILL, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);

    sf_device_init(NULL);
    snprintf(path, sizeof(path), "%s/ShadowFight2.s3e.raw", sf_data_dir);
    if (sf_s3e_load(&sf_game, path, load_addr, sf_import_resolve, sf_import_stub) < 0)
        return 1;
    LOGI("%d imports left unresolved", sf_game.unresolved);
    sf_device_init(sf_game.config);

    LOGI("entering s3e entry %p", (void *)sf_game.entry);
    sf_game.entry();
    LOGI("s3e entry returned");
    return 0;
}
