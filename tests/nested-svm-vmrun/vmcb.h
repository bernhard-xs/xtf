/*
 * Header file defining the minimal VMCB structure and related constants used
 * by the nested-SVM VMRUN smoke test.
 */
#ifndef XTF_TESTS_NESTED_SVM_VMRUN_VMCB_H
#define XTF_TESTS_NESTED_SVM_VMRUN_VMCB_H

#include <xtf/types.h>

/* Structure representing a segment register in the VMCB state save area. */
struct vmcb_seg {
     /* Segment selector. */
    uint16_t sel;
    /* Segment attributes, encoding type, DPL, P, AVL, L, D/B and G bits. */
    uint16_t attr;
    /* Segment limit. */
    uint32_t limit;
    /* Segment base address. */
    uint64_t base;
};

/*
 * Minimal AMD SVM Virtual Machine Control Block layout.
 *
 * Only the fields required by the nested-svm-vmrun test are named; the rest
 * of the 4 KiB structure is preserved as anonymous reserved padding so the
 * named fields land at their architectural offsets.
 *
 * See AMD64 APM Volume 2, Appendix B VMCB Layout for the authoritative layout.
 *
 * This is intentionally not the full Xen vmcb_struct:
 *
 * The structure is only intended to support the initial nested-SVM VMRUN
 * smoke test, and may be refactored as the nested-SVM tests evolve.
 *
 * The offsets of the fields that are used are verified with static assertions
 * to ensure the structure layout matches the architectural VMCB layout.
 */
struct vmcb {
    /* Control area (offsets from start of VMCB). */

    /* vec0: Read (0-15) and write (16-31) intercept bits for CR registers. */
    uint16_t intercept_read_cr;       /* 0x000 */
    uint16_t intercept_write_cr;      /* 0x002 */

    /* vec1: Read (0-15) and write (16-31) intercept bits for DR registers. */
    uint16_t intercept_read_dr;       /* 0x004 */
    uint16_t intercept_write_dr;      /* 0x006 */

    /* vec2: Intercept bits (0-31) for exceptions. */
    uint32_t intercept_exceptions;    /* 0x008 */

    /* vec3,4,5: Intercept bits for instructions (0-31). */
    uint32_t intercept_insns_vec3;    /* 0x00C */
    uint32_t intercept_insns_vec4;    /* 0x010 */
    uint32_t intercept_insns_vec5;    /* 0x014 */

    uint8_t  _pad_018[0x03C - 0x018]; /* Reserved area up to offset 0x03C. */

    uint16_t pause_filter_threshold;  /* 0x03C */
    uint16_t pause_filter_count;      /* 0x03E */
    uint64_t iopm_base_pa;            /* 0x040 */
    uint64_t msrpm_base_pa;           /* 0x048 */
    uint64_t tsc_offset;              /* 0x050 */
    uint32_t asid;                    /* 0x058 */
    uint8_t  tlb_control;             /* 0x05C */
    uint8_t  _pad_05d[3];
    uint64_t vintr;                   /* 0x060 */
    uint64_t int_state;               /* 0x068 */
    uint64_t exitcode;                /* 0x070 */
    uint64_t exitinfo1;               /* 0x078 */
    uint64_t exitinfo2;               /* 0x080 */
    uint64_t exit_int_info;           /* 0x088 */
    uint64_t np_enable;               /* 0x090 */
    uint8_t  _pad_098[0x0a8 - 0x098];
    uint64_t event_inj;               /* 0x0A8 */
    uint64_t h_cr3;                   /* 0x0B0 */
    uint8_t  _pad_0b8[0x400 - 0x0b8];

    /* State save area.  Offsets are relative to 0x400. */
    struct vmcb_seg es;               /* 0x400 */
    struct vmcb_seg cs;               /* 0x410 */
    struct vmcb_seg ss;               /* 0x420 */
    struct vmcb_seg ds;               /* 0x430 */
    struct vmcb_seg fs;               /* 0x440 */
    struct vmcb_seg gs;               /* 0x450 */
    struct vmcb_seg gdtr;             /* 0x460 */
    struct vmcb_seg ldtr;             /* 0x470 */
    struct vmcb_seg idtr;             /* 0x480 */
    struct vmcb_seg tr;               /* 0x490 */
    uint8_t  _pad_4a0[0x4cb - 0x4a0];
    uint8_t  cpl;                     /* 0x4CB */
    uint32_t _pad_4cc;
    uint64_t efer;                    /* 0x4D0 */
    uint8_t  _pad_4d8[0x548 - 0x4d8];
    uint64_t cr4;                     /* 0x548 */
    uint64_t cr3;                     /* 0x550 */
    uint64_t cr0;                     /* 0x558 */
    uint64_t dr7;                     /* 0x560 */
    uint64_t dr6;                     /* 0x568 */
    uint64_t rflags;                  /* 0x570 */
    uint64_t rip;                     /* 0x578 */
    uint8_t  _pad_580[0x5d8 - 0x580];
    uint64_t rsp;                     /* 0x5D8 */
    uint8_t  _pad_5e0[0x5f8 - 0x5e0];
    uint64_t rax;                     /* 0x5F8 */

    uint8_t  _pad_tail[0x1000 - 0x600];
};

/*
 * Compile-time verification of architectural offsets for the fields used
 * by this nested-SVM VMRUN Smoke test.
 */
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

/* General Intercept 1 register (offset 0x00C). */
#define GENERAL1_INTERCEPT_HLT          (1u << 24)
#define GENERAL1_INTERCEPT_SHUTDOWN_EVT (1u << 31)

/* General Intercept 2 register (offset 0x010). */
#define GENERAL2_INTERCEPT_VMRUN        (1u <<  0)
#define GENERAL2_INTERCEPT_VMMCALL      (1u <<  1)

/* Selected VMEXIT exit codes. */
#define VMEXIT_HLT                      0x078
#define VMEXIT_SHUTDOWN                 0x07f
#define VMEXIT_VMMCALL                  0x081

#endif /* XTF_NESTED_SVM_VMRUN_VMCB_H */

/*
 * Local variables:
 * mode: C
 * c-file-style: "BSD"
 * c-basic-offset: 4
 * tab-width: 4
 * indent-tabs-mode: nil
 * End:
 */
