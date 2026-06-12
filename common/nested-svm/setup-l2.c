#include <nested-svm/setup-l2.h>

/* AMD MSRs. */

/* The VMRUN host-save area */
#define MSR_VM_HSAVE_PA 0xc0010117U

/*
 * The L2 VMCB lives here. VMRUN auto-saves and restores the bulk of
 * L1's state via the host-save area pointed to by MSR_VM_HSAVE_PA.
 */
struct vmcb l2_vmcb __page_aligned_bss;

/* Backing store for the VMRUN host-save area (MSR_VM_HSAVE_PA). */
uint8_t hsave[PAGE_SIZE] __page_aligned_bss;

/* Stack used by L2.  Two pages of backing store. */
uint8_t l2_stack[2 * PAGE_SIZE] __page_aligned_bss;


static uint16_t user_desc_vmcb_attr(const user_desc *desc)
{
    return desc->type |
        (desc->s << 4) |
        (desc->dpl << 5) |
        (desc->p << 7) |
        (desc->limit1 << 8) |
        (desc->avl << 12) |
        (desc->l << 13) |
        (desc->d << 14) |
        (desc->g << 15);
}

static bool selector_is_null(uint16_t sel)
{
    return !(sel & ~(X86_SEL_TI | X86_SEL_RPL_MASK));
}

static void vmcb_set_seg_unusable(struct vmcb_seg *seg, uint16_t sel)
{
    seg->sel = sel;
    seg->attr = 0;
    seg->limit = 0;
    seg->base = 0;
}

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

    if ( (sel & X86_SEL_TI) ||
         (sel_offset + gdt_desc_bytes - 1 > gdt_limit) )
    {
        vmcb_set_seg_unusable(seg, 0);
        return;
    }

    desc = (const user_desc *)((const char *)gdt + sel_offset);

    if ( !desc->s )
        gdt_desc_bytes *= 2;

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

bool svm_l1_enable_svm()
{
    if ( !cpu_has_svm ) {
        xtf_skip("Skip: SVM not available\n");
        return false;
    }

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SVME);
    wrmsr(MSR_VM_HSAVE_PA, _u(hsave));
    return true;
}

void svm_l2_build_vmcb(struct vmcb *vmcb, const struct svm_l2_config *cfg)
{
    struct svm_l2_config default_l2 = {
        .efer = rdmsr(MSR_EFER),
        .intercept_insns_vec3 = GENERAL1_INTERCEPT_HLT,
        .intercept_insns_vec4 = GENERAL2_INTERCEPT_VMRUN,
        .rsp = _u(&l2_stack[sizeof(l2_stack)]),
    };
    desc_ptr gdt_desc, idt_desc;
    const user_desc *gdt;

    memset(vmcb, 0, sizeof(*vmcb));

    if ( !cfg )
        cfg = &default_l2;
    vmcb->intercept_insns_vec3 = cfg->intercept_insns_vec3;
    vmcb->intercept_insns_vec4 = cfg->intercept_insns_vec4;
    vmcb->intercept_insns_vec5 = cfg->intercept_insns_vec5;
    vmcb->asid = cfg->asid ? cfg->asid : 1;

    vmcb->cr0    = read_cr0();
    vmcb->cr3    = read_cr3();
    vmcb->cr4    = read_cr4();
    vmcb->efer   = cfg->efer;
    vmcb->rflags = read_flags();

    vmcb->rsp = cfg->rsp;
    vmcb->rip = cfg->rip;

    sgdt(&gdt_desc);
    sidt(&idt_desc);
    vmcb->gdtr.base  = gdt_desc.base;
    vmcb->gdtr.limit = gdt_desc.limit;
    vmcb->idtr.base  = idt_desc.base;
    vmcb->idtr.limit = idt_desc.limit;
    gdt = (const user_desc *)gdt_desc.base;

    vmcb_set_seg_desc(&vmcb->ldtr, gdt, gdt_desc.limit, sldt());
    vmcb_set_seg_desc(&vmcb->tr, gdt, gdt_desc.limit, str());

    vmcb->cs.sel = __KERN_CS;
    vmcb->cs.attr = 0xa9b;
    vmcb->cs.limit = ~0u;

    vmcb->ds.sel = __USER_DS;
    vmcb->ds.attr = 0xcf3;
    vmcb->ds.limit = ~0u;
    vmcb->es = vmcb->fs = vmcb->gs = vmcb->ds;

    vmcb->ss.sel = __KERN_DS;
    vmcb->ss.attr = 0;
    vmcb->ss.limit = 0;
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
