Nested SVM L2 VMRUN Reproducer
==============================

This is a minimized Xen nested-SVM reproducer, not a normal regression test.

Why This Exists
---------------

The L2 VMLOAD reproducer shows that Xen can currently kill or lose the guest
before L2 makes progress.  This companion reproducer narrows the problem by
checking the much simpler path where L2 executes ``VMRUN`` and L1 should see a
clean ``VMEXIT_VMRUN`` intercept.

What It Does
------------

L1 enables SVM, builds a minimal long-mode L2 VMCB with the architecturally
required ``VMRUN`` intercept set, enters L2 once, and has L2 execute a single
``VMRUN`` using an aligned operand in ``RAX``.

If Xen handles the nested path correctly, L1 should return from ``svm_vmrun()``
with ``exitcode == VMEXIT_VMRUN`` and L2 should not have progressed past the
intercepted instruction.

If Xen still exhibits the observed bug, the domain may crash or report an
unexpected exit code instead of a clean nested intercept.

How To Use It
-------------

Run the test directly by name when debugging Xen nested-SVM behaviour.  It is
kept in the ``special`` category so it stays out of the default test sweep.

Correlate the guest outcome with ``xl dmesg`` output immediately afterwards to
inspect Xen's nested-SVM diagnostics.