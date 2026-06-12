Nested SVM VMLOAD Negative Test
===============================

This test exercises the architectural VMLOAD failure cases that
are reachable from an hvm64 L1 guest using Xen nested SVM.

What It Verifies
----------------

The test verifies the distinct VMLOAD error classes that are reachable from
this hvm64 harness:

* VMLOAD with EFER.SVME clear.
* VMLOAD executed at CPL > 0.
* VMLOAD executed with malformed VMCB physical addresses in RAX.

How The Verification Functions Work
-----------------------------------

``test_main()`` supplies the VMLOAD-specific negative-case matrix to the shared
runner and checks that Xen reports the same exceptions that the AMD
architecture defines for the same preconditions, within the limits of an hvm64
long-mode harness.

``svm_negative_check_cases()`` toggles EFER.SVME as required by each subtest,
dispatches the instruction in kernel or user context, and restores the original
EFER value afterwards.

``stub_vmload()`` executes VMLOAD directly in L1 with a caller-supplied RAX
value and records any fault via the XTF exception-table helpers.

``user_vmload()`` executes the same instruction through ``exec_user_param()``
so the test can verify the CPL > 0 cases with the exact operand chosen by L1.

The AMD manuals also describe failure conditions for execution outside the
protected-mode environment required by SVM instructions, but those contexts are
not practically reachable from this hvm64 XTF harness without switching out of
the environment under test.  The test therefore covers all distinct error
classes that are reachable here.
