/**
 * @file tests/nested-svm-clgi-stgi/main.c
 * @ref test-nested-svm-clgi-stgi
 *
 * @page test-nested-svm-clgi-stgi nested-svm-clgi-stgi
 *
 * Smoke-test of AMD SVM nested virtualisation:
 *
 * An L1 guest:
 * 1. enables SVM,
 * 2. builds a minimal L2 VMCB that re-uses L1's address space, and
 * 3. uses CLGI/STGI to manage interrupts in L2.
 *
 * L2:
 * 1. increments %rax
 * 2. signals completion with HLT, which causes a #VMEXIT back to L1.
 *
 * @see tests/nested-svm-clgi-stgi/main.c
 */
#include <nested-svm/setup-l2.h>

const char test_title[] = "Nested SVM CLGI/STGI Smoke Test";

/**
 * L2 Interrupt Service Routine for Vector 0x30.
 * It is used to prove the interrupt was actually taken.
 */
static void __used l2_isr_0x30(void)
{
    /* Decrement rax before exit to L1 to indicate interrupt was taken */
    asm volatile ("dec %rax; hlt");
}

/**
 * Run a minimal L2 payload. If an interrupt is pending,
 * it will be taken after the instruction following STI.
 */
static void __used l2_entry(void)
{
    /* Signal success by incrementing %rax */
    asm volatile ("sti\n"
                  /* Dummy insn ensures STI takes effect before inc %rax */
                  "nop\n" 
                  "inc %rax\n"
                  "hlt");
}

static bool run_l2(void)    
{
    print_v_intr_ctrl(l2_vmcb.v_intr_ctrl, "pre-VMRUN ");
    asm volatile("mov %0, %%rax\n"
                 "vmload %%rax\n"
                 "clgi\n"
                 "vmrun %%rax\n"
                 "vmsave %%rax\n"
                 :
                 : "r" (_u(&l2_vmcb))
                 : "%rax", "memory");
    print_v_intr_ctrl(l2_vmcb.v_intr_ctrl, "post-VMRUN");
    printk("L1: returned from L2 (rax: 0x%lx)\n", l2_vmcb.rax);

    if ( l2_vmcb.exitcode != VMEXIT_HLT )
    {
        xtf_failure("unexpected L2 exit: 0x%lx (%s)\n", l2_vmcb.exitcode,
                    vmexit_reason(l2_vmcb.exitcode));
        return false;
    }
    return true;
}

/**
 * Execute the nested-SVM CLGI/STGI smoke test.
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

    /* Set up L2's IDTR and install the interrupt gate used by the test. */
    setup_l2_idt(&l2_vmcb, 0x30, l2_isr_0x30);

    /* Inject a pending virtual hardware interrupt (Vector 0x30, Priority 2) */
    l2_vmcb.v_intr_ctrl.fields.v_intr_vector = 0x30;
    l2_vmcb.v_intr_ctrl.fields.v_irq         = 1; /* vIRQ request enable bit */
    l2_vmcb.v_intr_ctrl.fields.v_intr_prio   = 2; /* vIRQ priority */

    /* Set the L2 entry point to this test's l2_entry function. */
    l2_vmcb.rax = 2; /* Starting value for L2 to inc/decrement before HLT */
    l2_vmcb.rip = _u(l2_entry);

    if (!run_l2())
        return;

    if ( l2_vmcb.rax != 1 ) /* L2 should have decremented %rax from 2 to 1 */
        return xtf_failure("unexpected L2 %%rax: 0x%lx\n", l2_vmcb.rax);

    l2_vmcb.v_intr_ctrl.fields.v_gif_enable = 1; /* Enable vGIF for L2 */
    l2_vmcb.v_intr_ctrl.fields.v_gif = 1; /* Set vGIF to allow interrupts */

    l2_vmcb.v_intr_ctrl.fields.v_irq         = 1; /* vIRQ request enable bit */
    if (!run_l2())
        return;

    if ( l2_vmcb.rax != 1 ) /* L2 should have decremented %rax from 2 to 1 */
        return xtf_failure("unexpected L2 %%rax: 0x%lx\n", l2_vmcb.rax);
        
    l2_vmcb.v_intr_ctrl.fields.v_gif_enable = 1; /* Enable vGIF for L2 */
    l2_vmcb.v_intr_ctrl.fields.v_gif = 0; /* disable interrupts */

    l2_vmcb.v_intr_ctrl.fields.v_irq         = 1; /* vIRQ request enable bit */
    if (!run_l2())
        return;

    if ( l2_vmcb.rax != 3 ) /* L2 should have incremented %rax from 2 to 3 */
        return xtf_failure("unexpected L2 %%rax: 0x%lx\n", l2_vmcb.rax);

    xtf_success(NULL);
}
