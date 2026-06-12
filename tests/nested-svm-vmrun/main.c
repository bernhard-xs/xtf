/**
 * @file tests/nested-svm-vmrun/main.c
 * @ref test-nested-svm-vmrun
 *
 * @page test-nested-svm-vmrun nested-svm-vmrun
 *
 * Smoke-test of AMD SVM nested virtualisation:
 *
 * An L1 guest:
 * 1. enables SVM,
 * 2. builds a minimal L2 VMCB that re-uses L1's address space, and
 * 3. uses VMRUN to enter an L2 callback.
 *
 * L2:
 * 1. increments %rax
 * 2. signals completion with HLT, which causes a #VMEXIT back to L1.
 *
 * @see tests/nested-svm-vmrun/main.c
 */
#include <nested-svm/setup-l2.h>

const char test_title[] = "Nested SVM VMRUN Smoke Test";

/**
 * Run a minimal L2 payload and report success back to L1.
 */
static void __used l2_entry(void)
{
    asm volatile ("inc %rax;hlt"); /* Signal success by incrementing %rax */
}

/**
 * Execute the nested-SVM VMRUN smoke test.
 *
 * L1 enables SVM, prepares a minimal L2 VMCB, enters L2 once with VMRUN
 * and verifies that L2 reports success before exiting with HLT.
 */
void test_main(void)
{
    /* Enable SVM, arm the host-save area and build the L2 VMCB. */
    if (!svm_l1_enable_svm())
        return;
    svm_l2_build_vmcb(&l2_vmcb, NULL);

    /* Set the L2 entry point to this test's l2_entry function. */
    l2_vmcb.rip = _u(l2_entry);

    printk("L1: entering L2 via VMRUN\n");
    asm volatile("mov %0, %%rax\n"
                 "vmload %%rax\n"
                 "vmrun %%rax\n"
                 "vmsave %%rax\n"
                 :
                 : "r" (_u(&l2_vmcb))
                 : "%rax", "memory");
    printk("L1: returned from L2 (rax 0x%lx)\n", l2_vmcb.rax);

    if ( l2_vmcb.exitcode != VMEXIT_HLT )
        return xtf_failure("unexpected L2 exit: 0x%lx\n", l2_vmcb.exitcode);

    if ( l2_vmcb.rax != 1 ) /* L2 should have incremented %rax from 0 to 1 */
        return xtf_failure("unexpected L2 %%rax: 0x%lx\n", l2_vmcb.rax);

    xtf_success(NULL);
}
