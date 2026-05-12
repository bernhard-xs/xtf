Nested SVM VMRUN HSAVE Restore Reproducer
=========================================

This is a temporary Xen reproducer, not a normal regression test.

Why This Exists
---------------

On the current Xen host, a nested-SVM VMRUN smoke test can complete its normal
guest-visible checks and then crash when L1 tries to restore its pre-test SVM
MSR state.

In the failing configuration, restoring ``MSR_VM_HSAVE_PA`` after the nested
VMRUN path triggers a guest ``#GP`` and Xen subsequently leaves anonymous
shutdown domains in ``--pscd`` state.

What It Does
------------

This test is a copy of the ordinary ``nested-svm-vmrun`` smoke test with one
additional teardown step: after L2 returns successfully, L1 attempts to restore
the original ``MSR_VM_HSAVE_PA`` and ``EFER`` values it observed before enabling
SVM for the test.

On a Xen build without the bug, that restore should be harmless.

On the current Xen build, the restore path is the point of failure and is kept
here as a direct reproducer for Xen-side debugging.

How To Use It
-------------

Run the test directly by name when debugging the Xen-side nested-SVM teardown
issue.  It is kept in the ``special`` category so it stays out of the default
test sweep.