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

/* Enable SVM in L1 and program the host-save area used by VMRUN. */
void svm_l1_prepare_for_vmrun(const void *hsave);

/* Leave L1 out of nested-hypervisor mode before guest shutdown. */
void svm_l1_finish_vmrun(void);

/* Build a minimal long-mode L2 VMCB that reuses the current L1 environment. */
void svm_l2_build_vmcb(struct vmcb *vmcb, const struct svm_l2_config *cfg);

/* Enter an L2 guest via the shared VMLOAD/VMRUN/VMSAVE trampoline. */
void svm_vmrun(unsigned long l2_vmcb_pa);

#endif /* XTF_TESTS_NESTED_SVM_L2_H */
