#ifndef XTF_NESTED_SVM_SETUP_L2_H
#define XTF_NESTED_SVM_SETUP_L2_H

#include <xtf.h>

#include "vmcb.h"

/* Minimal caller-supplied state for building an L2 VMCB from L1. */
struct svm_l2_config {
    unsigned long rip;
    unsigned long rsp;
    uint32_t asid;
    uint32_t intercept_insns_vec3;
    uint32_t intercept_insns_vec4;
    uint32_t intercept_insns_vec5;
    uint64_t efer;
};

extern struct vmcb l2_vmcb __page_aligned_bss;

/* Backing store for the VMRUN host-save area (MSR_VM_HSAVE_PA). */
extern uint8_t hsave[PAGE_SIZE] __page_aligned_bss;

/* Stack used by L2.  Two pages of backing store. */
extern uint8_t l2_stack[2 * PAGE_SIZE] __page_aligned_bss;

/* Enable SVM in L1 and program the host-save area used by VMRUN. */
bool svm_l1_enable_svm(void);

/* Leave L1 out of nested-hypervisor mode before guest shutdown. */
void svm_l1_finish_vmrun(void);

/* Build a minimal long-mode L2 VMCB that reuses the current L1 environment. */
void svm_l2_build_vmcb(struct vmcb *vmcb, const struct svm_l2_config *cfg);

/* Build an L2 IDT with a single interrupt gate for the supplied vector. */
void setup_l2_idt(struct vmcb *vmcb, unsigned int vector,
                  void (*handler)(void));

/* Enter an L2 guest via the shared VMLOAD/VMRUN/VMSAVE trampoline. */
void svm_vmrun(unsigned long l2_vmcb_pa);

#endif /* XTF_NESTED_SVM_SETUP_L2_H */
