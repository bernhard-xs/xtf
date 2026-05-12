/**
 * @file tests/nested-svm-l2-vmload-repro/main.c
 * @ref test-nested-svm-l2-vmload-repro
 *
 * @page test-nested-svm-l2-vmload-repro nested-svm-l2-vmload-repro
 *
 * Minimized reproducer for Xen nested-SVM handling of L2 VMLOAD.
 *
 * The test is intentionally not part of the default XTF sweep.  It exists to
 * reproduce and diagnose Xen behaviour when an L2 guest executes VMLOAD.
 *
 * @include tests/nested-svm-l2-vmload-repro/index.rst
 *
 * @see tests/nested-svm-l2-vmload-repro/main.c
 */
#include <xtf.h>

#include "../nested-svm/l2.h"

const char test_title[] = "Nested SVM L2 VMLOAD reproducer";

static struct vmcb l2_vmcb __page_aligned_bss;
static uint8_t hsave[PAGE_SIZE] __page_aligned_bss;
static uint8_t l2_stack[2 * PAGE_SIZE] __page_aligned_bss;
static uint8_t operand_page[PAGE_SIZE] __page_aligned_bss;

static volatile uint64_t l2_progress;

#define L2_ENTERED 0x1u
#define L2_AFTER_VMLOAD 0x2u

/**
 * Execute a single VMLOAD in L2 and record whether execution progressed past
 * the instruction.
 */
static void __used l2_entry(void)
{
    l2_progress = L2_ENTERED;

    asm volatile ("mov %[paddr], %%rax; vmload %%rax"
                  :
                  : [paddr] "r" (_u(operand_page))
                  : "rax", "memory");

    l2_progress = L2_AFTER_VMLOAD;

    for ( ;; )
        asm volatile ("hlt");
}

/**
 * Build and run the minimal L2 VMLOAD reproducer.
 */
void test_main(void)
{
    bool passed = false;

    const struct svm_l2_config cfg = {
        .rip = _u(l2_entry),
        .rsp = _u(&l2_stack[sizeof(l2_stack)]),
        .asid = 1,
        .intercept_insns_vec3 =
            GENERAL1_INTERCEPT_HLT | GENERAL1_INTERCEPT_SHUTDOWN_EVT,
        .intercept_insns_vec4 = GENERAL2_INTERCEPT_VMRUN,
        .efer = rdmsr(MSR_EFER),
    };

    if ( !cpu_has_svm )
        return xtf_skip("Skip: SVM not available\n");

    l2_progress = 0;

    svm_l1_prepare_for_vmrun(hsave);
    svm_l2_build_vmcb(&l2_vmcb, &cfg);

    printk("L1: entering L2 reproducer\n");
    svm_vmrun(_u(&l2_vmcb));
    printk("L1: L2 returned with exitcode 0x%lx, progress 0x%lx\n",
           l2_vmcb.exitcode, (unsigned long)l2_progress);

    if ( l2_vmcb.exitcode != VMEXIT_HLT )
    {
        xtf_failure("Fail: unexpected L2 exit 0x%lx\n",
                    l2_vmcb.exitcode);
        goto out;
    }

    if ( l2_progress != L2_AFTER_VMLOAD )
    {
        xtf_failure("Fail: L2 progress 0x%lx != expected 0x%x\n",
                    (unsigned long)l2_progress, L2_AFTER_VMLOAD);
        goto out;
    }

    passed = true;

 out:
    svm_l1_finish_vmrun();

    if ( passed )
        xtf_success("VMLOAD in L2 completed; Xen bug not reproduced\n");
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