/**
 * @file tests/nested-svm-vmrun-hsave-restore-repro/main.c
 * @ref test-nested-svm-vmrun-hsave-restore-repro
 *
 * @page test-nested-svm-vmrun-hsave-restore-repro nested-svm-vmrun-hsave-restore-repro
 *
 * Temporary reproducer for Xen nested-SVM teardown after VMRUN.
 *
 * This copies the normal nested-SVM VMRUN smoke test and then restores the
 * original L1 SVM MSR state after L2 returns.  On the current Xen host, the
 * restore path triggers a guest #GP on the HSAVE MSR write and leaves Xen with
 * an anonymous shutdown domain.
 *
 * @include tests/nested-svm-vmrun-hsave-restore-repro/index.rst
 *
 * @see tests/nested-svm-vmrun-hsave-restore-repro/main.c
 */
#include <xtf.h>

#include "../nested-svm/l2.h"

/* AMD MSRs. */
#define MSR_VM_HSAVE_PA 0xc0010117U

const char test_title[] = "Nested SVM VMRUN HSAVE restore reproducer";

struct svm_l1_host_state {
    uint64_t efer;
    uint64_t hsave_pa;
};

static struct vmcb l2_vmcb __page_aligned_bss;
static uint8_t hsave[PAGE_SIZE] __page_aligned_bss;
static uint8_t l2_stack[2 * PAGE_SIZE] __page_aligned_bss;

#define L2_SENTINEL 0xc0ffeeULL
static volatile uint64_t l2_handshake;

static void save_svm_host_state(struct svm_l1_host_state *state)
{
    state->efer = rdmsr(MSR_EFER);
    state->hsave_pa = rdmsr(MSR_VM_HSAVE_PA);
}

static void restore_svm_host_state(const struct svm_l1_host_state *state)
{
    wrmsr(MSR_VM_HSAVE_PA, state->hsave_pa);
    wrmsr(MSR_EFER, state->efer);
}

static void __used l2_entry(void)
{
    l2_handshake = L2_SENTINEL;
    for ( ;; )
        asm volatile ("hlt");
}

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

void test_main(void)
{
    struct svm_l1_host_state host_state;

    if ( !cpu_has_svm )
        return xtf_skip("Skip: SVM not available\n");

    save_svm_host_state(&host_state);
    svm_l1_prepare_for_vmrun(hsave);

    build_l2_vmcb();

    printk("L1: entering L2 via VMRUN\n");
    svm_vmrun(_u(&l2_vmcb));
    printk("L1: returned from L2 (exitcode 0x%lx, handshake 0x%lx)\n",
           l2_vmcb.exitcode, (unsigned long)l2_handshake);

    if ( l2_vmcb.exitcode != VMEXIT_HLT )
        xtf_failure("Fail: unexpected L2 exit 0x%lx (expected HLT 0x%x)\n",
                    l2_vmcb.exitcode, VMEXIT_HLT);
    else if ( l2_handshake != L2_SENTINEL )
        xtf_failure("Fail: L2 handshake 0x%lx != expected 0x%lx\n",
                    (unsigned long)l2_handshake,
                    (unsigned long)L2_SENTINEL);
    else
        xtf_success(NULL);

    restore_svm_host_state(&host_state);
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