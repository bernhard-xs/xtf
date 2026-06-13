/*
 * GDB-controllable nested-SVM RPC slave for Python-driven tests.
 *
 * This guest implements a simple RPC loop: Python sets current_cmd and cmd_arg
 * through GDB, this code executes the requested operation, stores the result in
 * cmd_status, and traps back to GDB with int3.
 */

#include <xtf.h>

#include <nested-svm/setup-l2.h>

const char test_title[] = "Nested-SVM Slave";

enum slave_cmd {
    CMD_IDLE = 0,
    CMD_CLGI = 1,
    CMD_STGI = 2,
    CMD_VMRUN = 3,
};

/* Global RPC interface variables, visible to GDB via DWARF symbols. */
volatile uint32_t current_cmd;
volatile uint64_t cmd_arg;
volatile uint64_t cmd_status;

/* L1 guest VMCB, globally visible for GDB/Python access. */
struct vmcb l1_vmcb __page_aligned_bss;

__attribute__((noinline, used)) static void gdb_slave_trap(void)
{
    asm volatile("" ::: "memory");
}

void test_main(void)
{
    if (!svm_l1_enable_svm())
        return;

    cmd_status = 0;
    current_cmd = CMD_IDLE;

    /* Signal Python that the slave is alive and ready for commands. */
    gdb_slave_trap();

    /* Infinite RPC loop: wait for command, execute, store result, trap back. */
    for (;;) {
        uint64_t fault = 0;

        switch (current_cmd) {
        case CMD_IDLE:
            break;

        case CMD_CLGI:
            asm volatile("clgi" ::: "memory");
            break;

        case CMD_STGI:
            asm volatile("stgi" ::: "memory");
            break;

        case CMD_VMRUN:
            asm volatile("mov %0, %%rax\n"
                         "vmload %%rax\n"
                         "vmrun %%rax\n"
                         "vmsave %%rax\n"
                         :
                         : "r" (_u(&l2_vmcb))
                         : "%rax", "memory");

            fault = l1_vmcb.exitcode;
            break;

        default:
            fault = ~0ULL;
            break;
        }

        cmd_status = fault;
        current_cmd = CMD_IDLE;

        /* Trap back to GDB for the next Python step. */
        gdb_slave_trap();
    }
}

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
