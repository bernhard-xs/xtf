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

 * L2:
 * 1. writes a sentinel into shared memory, and
 * 2. signals completion with HLT, which causes a #VMEXIT back to L1.

 * The test passes if:
 * 1. VMRUN succeeds,
 * 2. the exit reason is HLT, and
 * 3. L1 observes the expected sentinel value.
 *
 * @include tests/nested-svm-vmrun/index.rst
 *
 * @see tests/nested-svm-vmrun/main.c
 */
#include <xtf.h>

#include "vmcb.h"

/* AMD MSRs. */
#define MSR_VM_HSAVE_PA 0xc0010117U

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

/**
 * Enter L2 using the VMRUN trampoline in entry.S.
 * @param l2_vmcb_pa Physical address of the L2 VMCB to run.
 *
 * The trampoline issues VMLOAD, VMRUN and VMSAVE around the nested guest entry
 * so that L2 consumes the segment and state fields prepared in @c l2_vmcb.
 */
void svm_vmrun(unsigned long l2_vmcb_pa);

/* Sentinel written by L2 and verified by L1. */
#define L2_SENTINEL 0xc0ffeeULL
static volatile uint64_t l2_handshake;

/**
 * Convert an XTF segment descriptor into the VMCB attribute encoding.
 * @param desc Descriptor to translate.
 * @return VMCB segment attribute bits for @p desc.
 *
 * The VMCB stores descriptor limit[19:16] in attr[11:8], so the conversion
 * is not a direct copy of the x86 descriptor access bits.
 */
static uint16_t user_desc_vmcb_attr(const user_desc *desc)
{
    return desc->type |
        (desc->s << 4) |
        (desc->dpl << 5) |
        (desc->p << 7) |
        /* Descriptor limit[19:16] is encoded in attr[11:8]. */
        (desc->limit1 << 8) |
        (desc->avl << 12) |
        (desc->l << 13) |
        (desc->d << 14) |
        (desc->g << 15);
}

/**
 * Tell whether a selector names the architecturally null descriptor.
 * @param sel Selector to inspect.
 * @return True if @p sel references GDT entry 0 regardless of RPL.
 */
static bool selector_is_null(uint16_t sel)
{
    return !(sel & ~(X86_SEL_TI | X86_SEL_RPL_MASK));
}

/**
 * Mark a VMCB segment as unusable.
 * @param seg Segment state to update.
 * @param sel Selector value to retain in the VMCB.
 *
 * Null and architecturally invalid selectors are represented in the VMCB by
 * a zero attribute field with zero base and limit.
 */
static void vmcb_set_seg_unusable(struct vmcb_seg *seg, uint16_t sel)
{
    seg->sel = sel;
    seg->attr = 0;
    seg->limit = 0;
    seg->base = 0;
}

/**
 * Populate a VMCB segment field from an L1 GDT selector.
 * @param seg Segment field in the VMCB to update.
 * @param gdt Base of the L1 GDT.
 * @param gdt_limit Inclusive byte limit of the GDT.
 * @param sel Selector to decode.
 *
 * The helper rejects LDT selectors because this test only reuses the current
 * GDT.  In long mode, system descriptors such as TR and LDTR consume two GDT
 * slots, so both slots must fit within @p gdt_limit before the descriptor is
 * copied into the VMCB.
 */
static void vmcb_set_seg_desc(struct vmcb_seg *seg, const user_desc *gdt,
                              uint16_t gdt_limit, uint16_t sel)
{
    uint16_t sel_offset = sel & ~(X86_SEL_TI | X86_SEL_RPL_MASK);
    unsigned int gdt_desc_bytes = sizeof(*gdt);
    const user_desc *desc;

    if ( selector_is_null(sel) )
    {
        vmcb_set_seg_unusable(seg, sel);
        return;
    }

    /* Verify the descriptor's first slot still lies within the GDT limit. */
    if ( (sel & X86_SEL_TI) ||
         (sel_offset + gdt_desc_bytes - 1 > gdt_limit) )
    {
        vmcb_set_seg_unusable(seg, 0);
        return;
    }

    desc = (const user_desc *)((const char *)gdt + sel_offset);

    /* 64-bit long mode system descriptors (TR/LDTR) require two GDT slots. */
    if ( !desc->s )
        gdt_desc_bytes *= 2;

    /* Verify the descriptor's last byte still lies within the GDT limit. */
    if ( sel_offset + gdt_desc_bytes - 1 > gdt_limit )
    {
        vmcb_set_seg_unusable(seg, 0);
        return;
    }

    seg->sel = sel;
    seg->attr = user_desc_vmcb_attr(desc);
    seg->limit = user_desc_limit(desc);
    seg->base = user_desc_base(desc);
}

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
    desc_ptr gdt_desc, idt_desc;
    const user_desc *gdt;

    /* Intercept VMRUN (architecturally required) and HLT (used by L2 to
     * signal completion).  Also intercept SHUTDOWN so L2 mistakes surface
     * as a recognisable VMEXIT rather than killing the L1 domain. */
    l2_vmcb.intercept_insns_vec3 =
        GENERAL1_INTERCEPT_HLT | GENERAL1_INTERCEPT_SHUTDOWN_EVT;
    l2_vmcb.intercept_insns_vec4 = GENERAL2_INTERCEPT_VMRUN;

    /* ASID 0 is reserved for the host/hypervisor, so use ASID 1 for L2. */
    l2_vmcb.asid = 1;

    /*
     * Inherit L1's paging/control state, EFER and RFLAGS.
     *
     * This is a standard minimal nested test technique that avoids
     * the need to set up full paging and control structures for L2,
     * and sharing the GDT/IDT is acceptable for this smoke test.
     */
    l2_vmcb.cr0    = read_cr0();
    l2_vmcb.cr3    = read_cr3();
    l2_vmcb.cr4    = read_cr4();
    l2_vmcb.efer   = rdmsr(MSR_EFER);
    l2_vmcb.rflags = read_flags();

    l2_vmcb.rsp    = _u(&l2_stack[sizeof(l2_stack)]);
    l2_vmcb.rip    = _u(l2_entry);

    /* Inherit L1's GDT/IDT. */
    sgdt(&gdt_desc);
    sidt(&idt_desc);
    l2_vmcb.gdtr.base  = gdt_desc.base;
    l2_vmcb.gdtr.limit = gdt_desc.limit;
    l2_vmcb.idtr.base  = idt_desc.base;
    l2_vmcb.idtr.limit = idt_desc.limit;
    gdt = (const user_desc *)gdt_desc.base;

    /*
     * Mirror the active LDTR/TR from L1 into the L2 VMCB.  This test shares
     * L1's GDT, and VMLOAD/VMRUN consume LDTR/TR from the VMCB rather than
     * reconstructing them from the live CPU state.  Copying the current L1
     * descriptors keeps L2's system-segment state self-consistent with the
     * shared GDT; in particular, TR continues to describe the active TSS used
     * for long-mode event delivery (RSP0/IST), while any non-null LDTR state
     * is carried across unchanged.
     */
    vmcb_set_seg_desc(&l2_vmcb.ldtr, gdt, gdt_desc.limit, sldt());
    vmcb_set_seg_desc(&l2_vmcb.tr, gdt, gdt_desc.limit, str());

    /*
     * Flat 64-bit segments, mirroring how XTF sets up L1.
     * Encoded segment attributes (P|DPL|S|Type|...|G|D/B|L|AVL):
     */

    /* CS: present, ring 0, code, exec/read L=1 -> 0xa9b */
    l2_vmcb.cs.sel = __KERN_CS;
    l2_vmcb.cs.attr = 0xa9b;
    l2_vmcb.cs.limit = ~0u;

    /* DS/ES/FS/GS: present, ring 3, data, read/write, G=1 -> 0xcf3 (DPL3) */
    l2_vmcb.ds.sel = __USER_DS;
    l2_vmcb.ds.attr = 0xcf3;
    l2_vmcb.ds.limit = ~0u;
    l2_vmcb.es = l2_vmcb.fs = l2_vmcb.gs = l2_vmcb.ds;

    /*
     * SS: Long mode might ignore segment bases and limits, but it might still
     * require valid values for architectural reasons.
     * Same as above: present, ring 3, data, read/write, G=1 -> 0xcf3 (DPL3)
     */
    l2_vmcb.ss.sel = __KERN_DS;
    l2_vmcb.ss.attr = 0xcf3;
    l2_vmcb.ss.limit = ~0u;
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
    if ( !cpu_has_svm )
        return xtf_skip("Skip: SVM not available\n");

    /* Enable SVM and arm the host-save area. */
    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SVME);
    wrmsr(MSR_VM_HSAVE_PA, _u(hsave));

    build_l2_vmcb();

    printk("L1: entering L2 via VMRUN\n");
    svm_vmrun(_u(&l2_vmcb));
    printk("L1: returned from L2 (exitcode 0x%lx, handshake 0x%lx)\n",
           l2_vmcb.exitcode, (unsigned long)l2_handshake);

    if ( l2_vmcb.exitcode != VMEXIT_HLT )
        return xtf_failure("Fail: unexpected L2 exit 0x%lx (expected HLT 0x%x)\n",
                           l2_vmcb.exitcode, VMEXIT_HLT);

    if ( l2_handshake != L2_SENTINEL )
        return xtf_failure("Fail: L2 handshake 0x%lx != expected 0x%lx\n",
                           (unsigned long)l2_handshake,
                           (unsigned long)L2_SENTINEL);

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
