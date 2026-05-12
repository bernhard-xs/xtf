/*
 * SVM nested-virt VMRUN trampoline.
 *
 * Calling convention (System V AMD64):
 * %rdi - physical address of the L2 (guest) VMCB
 *
 * - VMRUN auto-saves a subset of host state to the area
 *   pointed to by MSR_VM_HSAVE_PA and reloads it on #VMEXIT.
 *
 * - Fields outside that subset
 *   (FS/GS/LDTR/TR base, KernelGSBase, syscall MSRs, SYSENTER_*)
 *   are exchanged via VMSAVE/VMLOAD on the L2 VMCB itself:
 *
 *   - VMLOAD before VMRUN loads L2's copy;
 *   - VMSAVE after #VMEXIT writes L2's back.
 *
 * Shared memory:
 * - This minimal test relies on L1 and L2 sharing compatible flat FS/GS state.
 *   so host-side preservation of FS/GS is not required for this minimal test.
 *
 * Enables interrupt handling:
 * 1. Nested guest execution requires GIF enabled for normal interrupt
 *    delivery. En entry, we STGI (enable interrupts: GIF=1) before VMRUN.
 # 2. #VMEXIT leaves GIF clear again, so we it by STGI before returning to L1.
 */
#include <xtf/asm_macros.h>

ENTRY(svm_vmrun)
        mov    %rdi, %rax
        vmload %rax
        stgi
        vmrun  %rax
        vmsave %rax
        stgi

        ret
ENDFUNC(svm_vmrun)
