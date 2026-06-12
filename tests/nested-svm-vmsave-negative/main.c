/**
 * @file tests/nested-svm-vmsave-negative/main.c
 * @ref test-nested-svm-vmsave-negative
 *
 * @page test-nested-svm-vmsave-negative nested-svm-vmsave-negative
 *
 * Negative testing of AMD SVM VMSAVE from an hvm64 L1 guest.
 *
 * The test exercises the reachable architectural VMSAVE failure cases in this
 * harness:
 * 1. SVM disabled via EFER.SVME clear.
 * 2. VMSAVE executed at CPL > 0.
 * 3. VMSAVE executed with malformed VMCB physical addresses in RAX.
 *
 * The test passes if Xen reflects the expected exception class for each case.
 *
 * @include tests/nested-svm-vmsave-negative/index.rst
 *
 * @see tests/nested-svm-vmsave-negative/main.c
 */
#include <nested-svm/test-harness.h>

const char test_title[] = "Nested SVM VMSAVE negative";

/* Aligned scratch page used to form candidate VMCB physical addresses. */
static uint8_t vmcb_page[PAGE_SIZE] __page_aligned_bss;

/**
 * Execute VMSAVE at CPL0 with a caller-supplied VMCB physical address.
 * @param paddr Candidate VMCB physical address for RAX.
 * @return Recorded exception information, or zero on success.
 */
static exinfo_t stub_vmsave(uint64_t paddr)
{
    exinfo_t fault = 0;

    asm volatile ("1: vmsave %%rax; 2:"
                  _ASM_EXTABLE_HANDLER(1b, 2b, %P[rec])
                  : "+D" (fault)
                  : "a" (paddr), [rec] "p" (ex_record_fault_edi)
                  : "memory");

    return fault;
}

/**
 * Execute VMSAVE at CPL3 using a caller-supplied VMCB physical address.
 * @param paddr Candidate VMCB physical address for RAX.
 * @return Recorded exception information, or zero on success.
 */
static unsigned long __user_text user_vmsave(unsigned long paddr)
{
    unsigned long fault = 0;

    asm volatile ("mov %[paddr], %%rax;"
                  "1: vmsave %%rax; xor %%eax, %%eax; 2:"
                  _ASM_EXTABLE_HANDLER(1b, 2b, %P[rec])
                  : "+a" (fault)
                  : [paddr] "r" (paddr),
                    [rec] "p" (ex_record_fault_eax)
                  : "memory");

    return fault;
}

static const struct svm_negative_ops vmsave_ops = {
    .kernel = stub_vmsave,
    .user = user_vmsave,
};

static const struct svm_negative_runner vmsave_runner = {
    .vmcb_page = vmcb_page,
    .ops = &vmsave_ops,
    .manage_host_svme = true,
};

/**
 * Execute the reachable VMSAVE negative-case matrix.
 *
 * The matrix covers the distinct bare-metal failure classes that can be
 * observed from an hvm64 L1 harness: EFER.SVME clear, CPL > 0, and malformed
 * VMCB physical addresses in RAX.
 */
void test_main(void)
{
    static const struct svm_negative_case cases[] = {
        {
            .name = "vmsave with SVME clear at CPL0",
            .svme = false,
            .mode = SVM_NEGATIVE_KERNEL,
            .paddr_kind = SVM_NEGATIVE_PADDR_ALIGNED,
            .expected = EXINFO_SYM(UD, 0),
        },
        {
            .name = "vmsave with SVME clear at CPL0 and unaligned paddr",
            .svme = false,
            .mode = SVM_NEGATIVE_KERNEL,
            .paddr_kind = SVM_NEGATIVE_PADDR_UNALIGNED,
            .expected = EXINFO_SYM(GP, 0),
        },
        {
            .name = "vmsave with SVME clear at CPL0 and overly wide paddr",
            .svme = false,
            .mode = SVM_NEGATIVE_KERNEL,
            .paddr_kind = SVM_NEGATIVE_PADDR_TOO_WIDE,
            .expected = EXINFO_SYM(UD, 0),
        },
        {
            .name = "vmsave with SVME clear at CPL3",
            .svme = false,
            .mode = SVM_NEGATIVE_USER,
            .paddr_kind = SVM_NEGATIVE_PADDR_ALIGNED,
            .expected = EXINFO_SYM(GP, 0),
        },
        {
            .name = "vmsave with SVME clear at CPL3 and unaligned paddr",
            .svme = false,
            .mode = SVM_NEGATIVE_USER,
            .paddr_kind = SVM_NEGATIVE_PADDR_UNALIGNED,
            .expected = EXINFO_SYM(GP, 0),
        },
        {
            .name = "vmsave with SVME clear at CPL3 and overly wide paddr",
            .svme = false,
            .mode = SVM_NEGATIVE_USER,
            .paddr_kind = SVM_NEGATIVE_PADDR_TOO_WIDE,
            .expected = EXINFO_SYM(GP, 0),
        },
        {
            .name = "vmsave with SVME set at CPL3",
            .svme = true,
            .mode = SVM_NEGATIVE_USER,
            .paddr_kind = SVM_NEGATIVE_PADDR_ALIGNED,
            .expected = EXINFO_SYM(GP, 0),
        },
        {
            .name = "vmsave with SVME set at CPL3 and unaligned paddr",
            .svme = true,
            .mode = SVM_NEGATIVE_USER,
            .paddr_kind = SVM_NEGATIVE_PADDR_UNALIGNED,
            .expected = EXINFO_SYM(GP, 0),
        },
        {
            .name = "vmsave with SVME set at CPL3 and overly wide paddr",
            .svme = true,
            .mode = SVM_NEGATIVE_USER,
            .paddr_kind = SVM_NEGATIVE_PADDR_TOO_WIDE,
            .expected = EXINFO_SYM(GP, 0),
        },
        {
            .name = "vmsave with SVME set at CPL0 and unaligned paddr",
            .svme = true,
            .mode = SVM_NEGATIVE_KERNEL,
            .paddr_kind = SVM_NEGATIVE_PADDR_UNALIGNED,
            .expected = EXINFO_SYM(GP, 0),
        },
        {
            .name = "vmsave with SVME set at CPL0 and overly wide paddr",
            .svme = true,
            .mode = SVM_NEGATIVE_KERNEL,
            .paddr_kind = SVM_NEGATIVE_PADDR_TOO_WIDE,
            .expected = EXINFO_SYM(GP, 0),
        },
    };

    if ( !svm_negative_supported() )
        return;

    svm_negative_check_cases(cases, ARRAY_SIZE(cases), &vmsave_runner);

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
