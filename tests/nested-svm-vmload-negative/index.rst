Nested SVM VMLOAD Negative
==========================

This test exercises the architectural VMLOAD failure cases that are reachable
from an hvm64 L1 guest using Xen nested SVM.

Why This Test Matters
---------------------

Nested-SVM support is not only about the success path.  Xen also needs to
reflect the same architectural failures that bare metal would report when an
L1 executes VMLOAD in the wrong context or with an invalid VMCB physical
address.

That makes negative testing important for compatibility: guest kernels and
guest hypervisors rely on VMLOAD failing with the correct exception class when
SVM is disabled, when the instruction is executed without sufficient
privilege, or when the VMCB address in RAX is malformed.

What It Verifies
----------------

The test verifies the reachable bare-metal-style VMLOAD error classes from an
hvm64 L1 environment.

For the architecturally valid kernel case with an aligned operand, clearing
EFER.SVME causes VMLOAD to raise #UD.  Once privilege or operand-shape checks
come into play, Xen's nested-SVM path in this harness reports #GP(0) for those
negative cases.

It also checks the architecturally relevant precedence that can be observed in
this harness:

* Privilege failure (#GP(0)) takes precedence over SVM-disabled execution.
* Malformed RAX contents (#GP(0)) take precedence over SVM-disabled execution.
* Privilege failure (#GP(0)) takes precedence over malformed RAX contents.

How The Verification Functions Work
-----------------------------------

``stub_vmload()`` executes VMLOAD directly in L1 with a caller-supplied RAX
value and records any fault via the XTF exception-table helpers.

``user_vmload()`` executes the same instruction through ``exec_user_param()``
so the test can verify the CPL > 0 cases with the exact operand chosen by L1.

``svm_negative_check_cases()`` from the shared nested-SVM negative helper layer
toggles EFER.SVME as required by each subtest, dispatches the instruction in
kernel or user context, and restores the original EFER value afterwards.

``test_main()`` supplies the VMLOAD-specific negative-case matrix to the shared
runner and checks that Xen reports the same exceptions that the AMD
architecture defines for the same preconditions, within the limits of an hvm64
long-mode harness.

The AMD manuals also describe failure conditions for execution outside the
protected-mode environment required by SVM instructions, but those contexts are
not practically reachable from this hvm64 XTF harness without switching out of
the environment under test.  The test therefore covers all distinct error
classes that are reachable here.