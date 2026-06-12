#ifndef XTF_NESTED_SVM_TEST_HARNESS_H
#define XTF_NESTED_SVM_TEST_HARNESS_H

#include <xtf.h>

enum svm_negative_mode {
    SVM_NEGATIVE_KERNEL,
    SVM_NEGATIVE_USER,
};

enum svm_negative_paddr_kind {
    SVM_NEGATIVE_PADDR_ALIGNED,
    SVM_NEGATIVE_PADDR_UNALIGNED,
    SVM_NEGATIVE_PADDR_TOO_WIDE,
};

struct svm_negative_case {
    const char *name;
    bool svme;
    enum svm_negative_mode mode;
    enum svm_negative_paddr_kind paddr_kind;
    exinfo_t expected;
};

struct svm_negative_ops {
    exinfo_t (*kernel)(uint64_t paddr);
    unsigned long (*user)(unsigned long paddr);
};

/* Return false when setup already reported a skip or failure for the case. */
typedef bool (*svm_negative_setup_fn)(const struct svm_negative_case *t,
                                      uint64_t paddr, void *data);
typedef void (*svm_negative_teardown_fn)(const struct svm_negative_case *t,
                                         uint64_t paddr, void *data);

/*
 * Shared execution context for a family of SVM negative cases.
 *
 * Optional setup/teardown hooks let higher-level tests prepare nested state
 * such as L2 or L3 control structures outside the fixed case matrix before
 * the helper dispatches the actual instruction.  L1-style callers can leave
 * manage_host_svme enabled so the helper toggles live EFER.SVME itself,
 * while nested callers can keep host SVME fixed and use setup/teardown to
 * manage guest-visible SVME state explicitly.
 */
struct svm_negative_runner {
    const void *vmcb_page;
    const struct svm_negative_ops *ops;
    bool manage_host_svme;
    void *data;
    svm_negative_setup_fn setup;
    svm_negative_teardown_fn teardown;
};

/* Check basic nested-SVM availability in the current environment. */
bool svm_negative_supported(void);

/* Execute and check the supplied case matrix with the shared runner. */
void svm_negative_check_cases(const struct svm_negative_case *cases,
                              unsigned int nr_cases,
                              const struct svm_negative_runner *runner);

#endif /* TESTS_NESTED_SVM_SVM_NEGATIVE_H */
