/*
 * Shared SVM nested-virt VMRUN trampoline for nested-SVM tests.
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