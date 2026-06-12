#include <nested-svm/test-harness.h>

static uint64_t svm_negative_paddr(const void *vmcb_page,
                                   enum svm_negative_paddr_kind kind)
{
    switch ( kind )
    {
    case SVM_NEGATIVE_PADDR_ALIGNED:
        return _u(vmcb_page);

    case SVM_NEGATIVE_PADDR_UNALIGNED:
        return _u(vmcb_page) | 1ul;

    case SVM_NEGATIVE_PADDR_TOO_WIDE:
        return 1ull << maxphysaddr;
    }

    unreachable();
}

/* set or clear EFER.SVME and return the original EFER value */
static uint64_t update_efer_svme(bool enable)
{
    uint64_t efer = rdmsr(MSR_EFER);
    uint64_t new_efer = enable ? (efer | EFER_SVME) : (efer & ~EFER_SVME);

    wrmsr(MSR_EFER, new_efer);

    return efer;
}

static bool svm_negative_run(const struct svm_negative_case *t,
                             const struct svm_negative_runner *runner,
                             exinfo_t *res)
{
    uint64_t paddr = svm_negative_paddr(runner->vmcb_page, t->paddr_kind);
    uint64_t orig_efer = 0;

    if ( runner->setup && !runner->setup(t, paddr, runner->data) )
        return false;

    if ( runner->manage_host_svme )
        orig_efer = update_efer_svme(t->svme);

    *res = 0;

    switch ( t->mode )
    {
    case SVM_NEGATIVE_KERNEL:
        *res = runner->ops->kernel(paddr);
        break;

    case SVM_NEGATIVE_USER:
        *res = exec_user_param(runner->ops->user, paddr);
        break;
    }

    if ( runner->manage_host_svme )
        wrmsr(MSR_EFER, orig_efer);

    if ( runner->teardown )
        runner->teardown(t, paddr, runner->data);

    return true;
}

static void svm_negative_check_case(const struct svm_negative_case *t,
                                    const struct svm_negative_runner *runner)
{
    exinfo_t got;

    if ( !svm_negative_run(t, runner, &got) )
        return;

    if ( got == t->expected )
        return;

    xtf_failure("Fail: %s, got %pe, expected %pe\n",
                t->name, _p(got), _p(t->expected));
}

bool svm_negative_supported(void)
{
    if ( !cpu_has_svm )
    {
        xtf_skip("Skip: SVM not available\n");
        return false;
    }

    return true;
}

void svm_negative_check_cases(const struct svm_negative_case *cases,
                              unsigned int nr_cases,
                              const struct svm_negative_runner *runner)
{
    unsigned int i;

    for ( i = 0; i < nr_cases; ++i )
        svm_negative_check_case(&cases[i], runner);
}
