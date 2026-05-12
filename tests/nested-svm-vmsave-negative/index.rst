Nested SVM VMSAVE Negative
==========================

This test exercises the architectural VMSAVE failure cases that are reachable
from an hvm64 L1 guest using Xen nested SVM.

Why This Test Matters
---------------------

Nested-SVM support needs to match the architectural failure behaviour of SVM
instructions, not only the success path.  Guest kernels and guest hypervisors
rely on VMSAVE faulting with the right exception class when the instruction is
used outside SVM, at insufficient privilege, or with a malformed VMCB physical
address.

What It Verifies
----------------

The test verifies the distinct VMSAVE error classes that are reachable from
this hvm64 harness:

* VMSAVE with EFER.SVME clear.
* VMSAVE executed at CPL > 0.
* VMSAVE executed with malformed VMCB physical addresses in RAX.

It also checks the observable precedence between these conditions in this Xen
environment, mirroring the approach used for the VMLOAD negative test.

How The Verification Functions Work
-----------------------------------

``stub_vmsave()`` executes VMSAVE directly in L1 with a caller-supplied RAX
value and records any fault via the XTF exception-table helpers.

``user_vmsave()`` executes the same instruction through ``exec_user_param()``
so the test can verify the CPL > 0 cases with the exact operand chosen by L1.

``svm_negative_check_cases()`` from the shared nested-SVM negative helper layer
toggles EFER.SVME as required by each subtest, dispatches the instruction in
kernel or user context, and restores the original EFER value afterwards.

``test_main()`` supplies the VMSAVE-specific negative-case matrix to the shared
runner and checks that Xen reports the expected exception class for each
reachable precondition from this hvm64 long-mode harness.

The AMD manuals also describe failures for contexts outside the protected-mode
environment required by SVM instructions, but those cases are not practically
reachable from this hvm64 XTF harness without leaving the environment under
test.  This test therefore focuses on the distinct negative cases that are
reachable here.