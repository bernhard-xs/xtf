/* Shared minimal VMCB definitions for nested-SVM tests. */
#ifndef XTF_TESTS_NESTED_SVM_VMCB_H
#define XTF_TESTS_NESTED_SVM_VMCB_H

#include <xtf/types.h>

struct vmcb_seg {
    uint16_t sel;
    uint16_t attr;
    uint32_t limit;
    uint64_t base;
};

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
    uint64_t vintr;
    uint64_t int_state;
    uint64_t exitcode;
    uint64_t exitinfo1;
    uint64_t exitinfo2;
    uint64_t exit_int_info;
    uint64_t np_enable;
    uint8_t  _pad_098[0x0a8 - 0x098];
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

#endif /* XTF_TESTS_NESTED_SVM_VMCB_H */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
