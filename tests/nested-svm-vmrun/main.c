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
 * 1. writes a sentinel into shared memory, and
 * 2. signals completion with HLT, which causes a #VMEXIT back to L1.
 *
 * The test passes if:
 * 1. VMRUN succeeds,
 * 2. the exit reason is HLT, and
 * 3. L1 observes the expected sentinel value.
 *
 * @include tests/nested-svm-vmrun/index.rst
 *
 * @see tests/nested-svm-vmrun/main.c
 */
#include <nested-svm/setup-l2.h>

const char test_title[] = "Nested SVM VMRUN";

/*
 * The L2 VMCB lives here. VMRUN auto-saves and restores the bulk of
 * L1's state via the host-save area pointed to by MSR_VM_HSAVE_PA.
 */
static struct vmcb l2_vmcb __page_aligned_bss;

/* Backing store for the VMRUN host-save area (MSR_VM_HSAVE_PA). */
static uint8_t hsave[PAGE_SIZE] __page_aligned_bss;

/* Stack used by L2.  Two pages of backing store. */
static uint8_t l2_stack[2 * PAGE_SIZE] __page_aligned_bss;

/* Sentinel written by L2 and verified by L1. */
#define L2_SENTINEL 0xc0ffeeULL
static volatile uint64_t l2_handshake;

/**
 * Run a minimal L2 payload and report success back to L1.
 *
 * L2 cannot use the inherited Xen hypercall page here because VMMCALL from L2
 * unconditionally causes a #VMEXIT to L1 in Xen's nested-SVM model.  Instead,
 * L2 writes a sentinel into shared memory and halts so L1 observes a clean
 * HLT exit reason.
 */
static void __used l2_entry(void)
{
    l2_handshake = L2_SENTINEL;
    for ( ;; )
        asm volatile ("hlt");
}

/**
 * Build the VMCB state used for the nested L2 guest.
 *
 * The test reuses L1's paging structures and descriptor tables, but supplies
 * its own RIP and stack so L2 can execute a small payload in L1's address
 * space.  The resulting VMCB is intentionally minimal: it only carries the
 * control and segment state needed for the VMRUN smoke test.
 */
static void build_l2_vmcb(void)
{
    const struct svm_l2_config cfg = {
        .rip = _u(l2_entry),
        .rsp = _u(&l2_stack[sizeof(l2_stack)]),
        .asid = 1,
        .intercept_insns_vec3 =
            GENERAL1_INTERCEPT_HLT | GENERAL1_INTERCEPT_SHUTDOWN_EVT,
        .intercept_insns_vec4 = GENERAL2_INTERCEPT_VMRUN,
        .efer = rdmsr(MSR_EFER),
    };

    svm_l2_build_vmcb(&l2_vmcb, &cfg);
}

/**
 * Execute the nested-SVM VMRUN smoke test.
 *
 * L1 enables SVM, prepares a minimal L2 VMCB, enters L2 once with VMRUN and
 * verifies that L2 reports success by writing the expected sentinel before
 * exiting with HLT.
 */
void test_main(void)
{
    bool passed = false;

    if ( !cpu_has_svm )
        return xtf_skip("Skip: SVM not available\n");

    /* Enable SVM and arm the host-save area. */
    svm_l1_prepare_for_vmrun(hsave);

    build_l2_vmcb();

    printk("L1: entering L2 via VMRUN\n");
    svm_vmrun(_u(&l2_vmcb));
    printk("L1: returned from L2 (exitcode 0x%lx, handshake 0x%lx)\n",
           l2_vmcb.exitcode, (unsigned long)l2_handshake);

    if ( l2_vmcb.exitcode != VMEXIT_HLT )
    {
        xtf_failure("Fail: unexpected L2 exit 0x%lx (expected HLT 0x%x)\n",
                    l2_vmcb.exitcode, VMEXIT_HLT);
        goto out;
    }

    if ( l2_handshake != L2_SENTINEL )
    {
        xtf_failure("Fail: L2 handshake 0x%lx != expected 0x%lx\n",
                    (unsigned long)l2_handshake,
                    (unsigned long)L2_SENTINEL);
        goto out;
    }

    passed = true;

 out:
    svm_l1_finish_vmrun();

    if ( passed )
        xtf_success(NULL);
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
