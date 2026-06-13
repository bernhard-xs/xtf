/* Shared minimal VMCB definitions for nested-SVM tests. */
#ifndef XTF_NESTED_SVM_VMCB_H
#define XTF_NESTED_SVM_VMCB_H

#include <xtf/types.h>

struct vmcb_seg {
    uint16_t sel;
    uint16_t attr;
    uint32_t limit;
    uint64_t base;
};

/* VMCB 0x060: Virtual Interrupt Control to inject virtual interrupts */
typedef union {
    uint64_t bytes;
    struct {
        uint64_t v_tpr          : 8;  /* 0:7   - Virtual Task Priority Register */
        uint64_t v_irq          : 1;  /* 8     - Virtual Interrupt Request */
        uint64_t v_gif          : 1;  /* 9     - Virtual Global Interrupt Flag */
        uint64_t reserved_1     : 1;  /* 10    - Reserved */
        uint64_t vnmi_pending   : 1;  /* 11    - Virtual NMI Pending */
        uint64_t vnmi_blocking  : 1;  /* 12    - Virtual NMI Blocking */
        uint64_t reserved_2     : 3;  /* 13:15 - Reserved */
        uint64_t v_intr_prio    : 4;  /* 16:19 - Virtual Interrupt Priority */
        uint64_t v_ign_tpr      : 1;  /* 20    - Virtual Ignore TPR */
        uint64_t reserved_3     : 3;  /* 21:23 - Reserved */
        uint64_t v_intr_masking : 1;  /* 24    - Virtual Interrupt Masking */
        uint64_t v_gif_enable   : 1;  /* 25    - Virtual GIF Enable */
        uint64_t vnmi_enable    : 1;  /* 26    - Virtual NMI Enable */
        uint64_t reserved_4     : 5;  /* 27:31 - Reserved (Bit 27 is AVIC_ENABLE on newer CPUs) */
        uint64_t v_intr_vector  : 8;  /* 32:39 - Virtual Interrupt Vector */
        uint64_t reserved_5     : 24; /* 40:63 - Reserved */
    } __attribute__((packed)) fields;
} v_intr_ctrl_t;

/* Function to print all bits of v_intr_ctrl_t */
static inline void print_v_intr_ctrl(v_intr_ctrl_t ctrl, const char *prefix)
{
    printk("IRQ-ctrl");
    if (prefix)
        printk(" %s", prefix);
    printk(":");
    /* Print a list of the bits which are set: */
    if (ctrl.fields.v_intr_prio)
        printk(" prio:%u", ctrl.fields.v_intr_prio);                
    if (ctrl.fields.v_intr_vector)
        printk(" vec:%x", ctrl.fields.v_intr_vector);
    if (ctrl.fields.vnmi_enable)
        printk(" vNMI-enabled");                
    if (ctrl.fields.v_gif_enable)
        printk(" vGIF:%u", ctrl.fields.v_gif);
    if (ctrl.fields.v_irq)
        printk(" IRQ-pending");
    if (ctrl.fields.vnmi_pending)
        printk(" vnmi_pending");
    if (ctrl.fields.vnmi_blocking)
        printk(" vnmi_blocking");

    if (ctrl.fields.v_tpr)
        printk(" tpr=%u", ctrl.fields.v_tpr);
    if (ctrl.fields.v_ign_tpr)
        printk(" ign_tpr");
    if (ctrl.fields.v_intr_masking)
        printk(" intr_masking");


    printk("\n");
}

struct vmcb {
    uint16_t intercept_read_cr;
    uint16_t intercept_write_cr;
    uint16_t intercept_read_dr;
    uint16_t intercept_write_dr;
    uint32_t intercept_exceptions;
    uint32_t intercept_insns_vec3;
    uint32_t intercept_insns_vec4;
    uint32_t intercept_insns_vec5;
    uint8_t  _pad_018[0x03C - 0x018];
    uint16_t pause_filter_threshold;
    uint16_t pause_filter_count;
    uint64_t iopm_base_pa;
    uint64_t msrpm_base_pa;
    uint64_t tsc_offset;
    uint32_t asid;
    uint8_t  tlb_control;
    uint8_t  _pad_05d[3];
    /* 0x060: Virtual Interrupt Control to inject virtual interrupts */
    v_intr_ctrl_t v_intr_ctrl;
    uint64_t int_state;
    uint64_t exitcode;
    uint64_t exitinfo1;
    uint64_t exitinfo2;
    uint64_t exit_int_info;
    uint64_t np_enable;
    uint8_t  _pad_098[0x0a8 - 0x098];
    /* 0x0a8: Event Injection */
    uint64_t event_inj;
    uint64_t h_cr3;
    uint8_t  _pad_0b8[0x400 - 0x0b8];
    struct vmcb_seg es;
    struct vmcb_seg cs;
    struct vmcb_seg ss;
    struct vmcb_seg ds;
    struct vmcb_seg fs;
    struct vmcb_seg gs;
    struct vmcb_seg gdtr;
    struct vmcb_seg ldtr;
    struct vmcb_seg idtr;
    struct vmcb_seg tr;
    uint8_t  _pad_4a0[0x4cb - 0x4a0];
    uint8_t  cpl;
    uint32_t _pad_4cc;
    uint64_t efer;
    uint8_t  _pad_4d8[0x548 - 0x4d8];
    uint64_t cr4;
    uint64_t cr3;
    uint64_t cr0;
    uint64_t dr7;
    uint64_t dr6;
    uint64_t rflags;
    uint64_t rip;
    uint8_t  _pad_580[0x5d8 - 0x580];
    uint64_t rsp;
    uint8_t  _pad_5e0[0x5f8 - 0x5e0];
    uint64_t rax;
    uint8_t  _pad_tail[0x1000 - 0x600];
};

#define VMCB_CHECK(field, offset) \
    _Static_assert(__builtin_offsetof(struct vmcb, field) == (offset), \
                   "VMCB layout mismatch: " #field)
VMCB_CHECK(intercept_insns_vec3, 0x00c);
VMCB_CHECK(intercept_insns_vec4, 0x010);
VMCB_CHECK(asid,                 0x058);
VMCB_CHECK(v_intr_ctrl,          0x060);
VMCB_CHECK(exitcode,             0x070);
VMCB_CHECK(es,                   0x400);
VMCB_CHECK(gdtr,                 0x460);
VMCB_CHECK(idtr,                 0x480);
VMCB_CHECK(tr,                   0x490);
VMCB_CHECK(efer,                 0x4d0);
VMCB_CHECK(cr4,                  0x548);
VMCB_CHECK(cr3,                  0x550);
VMCB_CHECK(cr0,                  0x558);
VMCB_CHECK(rflags,               0x570);
VMCB_CHECK(rip,                  0x578);
VMCB_CHECK(rsp,                  0x5d8);
VMCB_CHECK(rax,                  0x5f8);
_Static_assert(sizeof(struct vmcb) == 0x1000, "VMCB size != 4 KiB");
#undef VMCB_CHECK

#define GENERAL1_INTERCEPT_HLT          (1u << 24)
#define GENERAL1_INTERCEPT_SHUTDOWN_EVT (1u << 31)

#define GENERAL2_INTERCEPT_VMRUN        (1u <<  0)
#define GENERAL2_INTERCEPT_VMMCALL      (1u <<  1)

#define VMEXIT_HLT                      0x078
#define VMEXIT_SHUTDOWN                 0x07f
#define VMEXIT_VMRUN                    0x080
#define VMEXIT_VMMCALL                  0x081

/* Get the vmexit reason */
static const char __used *vmexit_reason(uint64_t exitcode)
{
    switch (exitcode) {
    case VMEXIT_HLT:      return "HLT";
    case VMEXIT_SHUTDOWN: return "SHUTDOWN";
    case VMEXIT_VMRUN:    return "VMRUN";
    case VMEXIT_VMMCALL:  return "VMMCALL";
    default:              return "UNKNOWN";
    }
}

#endif /* XTF_NESTED_SVM_VMCB_H */
