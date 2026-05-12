Xen Nested-SVM L2 Debugging Notes
=================================

Purpose
-------

These notes capture the current Xen-side symptoms seen while trying to extend
XTF nested-SVM coverage into L2.  They are intended to give Xen debugging a
stable starting point: one small VMLOAD reproducer, one small VMRUN
reproducer, and a short list of observed guest outcomes and ``xl dmesg``
signatures.

Reproducers
-----------

Use these special-category tests directly rather than through the default test
sweep:

* ``./xtf-runner test-hvm64-nested-svm-l2-vmload-repro``
* ``./xtf-runner test-hvm64-nested-svm-l2-vmrun-repro``
* ``./xtf-runner test-hvm64-nested-svm-vmrun-hsave-restore-repro``

Both tests rely on the shared nested-SVM helpers in ``tests/nested-svm/`` and
keep the guest setup intentionally minimal.

Confirmed Guest Outcomes
------------------------

Current Xen behaviour observed from the minimized L2 VMLOAD reproducer:

* L1 enters L2.
* L2 makes no visible progress before returning.
* L1 reports ``exitcode 0xffffffffffffffff`` and ``progress 0x0``.

Current Xen behaviour observed from the minimized L2 VMRUN reproducer:

* L1 enters L2.
* L2 makes no visible progress before returning unexpectedly.
* L1 again reports ``exitcode 0xffffffffffffffff`` and ``progress 0x0``.

Current Xen behaviour observed from exploratory L2 negative work before it was
retracted from the normal test tree:

* L2 execution of ``VMLOAD`` and ``VMSAVE`` could trigger Xen-side MMIO
  emulation failures instead of clean guest-visible exceptions.
* Earlier invalid guest setups also triggered Xen warnings about guest EFER and
  missing ``VMRUN`` intercept state; those warnings are useful for triage, but
  they were associated with a discarded exploratory matrix rather than the
  minimized reproducers.

Observed Xen Log Signatures
---------------------------

The most repeatable Xen signature seen while exercising L2 SVM instructions is
MMIO emulation failure on the SVM opcode bytes themselves, for example:

* ``MMIO emulation failed (1): ... -> 0f 01 da ...`` for ``VMLOAD``
* ``MMIO emulation failed (1): ... -> 0f 01 db ...`` in nearby shared nested-
  SVM entry/return paths during the VMRUN-oriented reproducer runs

Additional Xen diagnostics seen during earlier exploratory L2 negative work:

* ``nsvm_vmcb_prepare4vmrun: EFER: SVME bit not set (0x500)``
* ``nsvm_vmcb_prepare4vmrun: GENERAL2_INTERCEPT: VMRUN intercept bit is clear``
* ``vcpu_runstate_change: dNNN has no online vcpus!``

Interpretation Notes
--------------------

The EFER/intercept warnings above came from an earlier experimental L2 matrix
that deliberately exercised invalid guest setup.  They are still useful as a
reference for Xen-side assertions, but they should not be confused with the
smaller current reproducers.

The minimized VMLOAD reproducer keeps the L2 entry path valid and only asks L2
to execute one ``VMLOAD``.  If that reproducer still returns with
``exitcode 0xffffffffffffffff`` or produces MMIO-emulation diagnostics in Xen,
the remaining bug is no longer in the discarded guest setup logic.

The minimized VMRUN reproducer currently does not produce a clean
``VMEXIT_VMRUN`` either.  The latest fault addresses mapped back into the
shared nested-SVM helper path rather than conclusively into the guest
``l2_entry()`` body, which suggests the failure may happen while Xen is still
handling shared nested state around guest entry/return rather than in the
intended L2 post-entry control flow.

Teardown Observations
---------------------

The runner's temporary ``.stale-*`` rename used for fast immediate reruns is
not the whole story.  That renamed entry is visible only briefly after a new
run starts and is typically gone again within a few seconds.

On the current host, the immediate named ``--ps--`` entry visible right after a
successful run is just Xen's normal asynchronous destroy path.  The runner
returns early on purpose so short tests stay fast, which means a transient
named shutdown domain can still be visible for a moment.

The more interesting bug was the conversion of that transient entry into a
persistent anonymous shutdown domain.  A focused guest-side probe showed that
clearing ``EFER.SVME`` before guest shutdown prevents that conversion for the
ordinary ``nested-svm-vmrun`` smoke test and for the L2 reproducers which fail
through the shared nested ``svm_vmrun()`` path.

That makes the remaining teardown issue look more specific than "VMRUN itself
always poisons Xen teardown": Xen currently misbehaves when certain nested-SVM
state survives up to guest poweroff.

The current Xen-side teardown reproducer is now the dedicated
``nested-svm-vmrun-hsave-restore-repro`` test.  It copies the normal VMRUN
smoke test but restores the original ``MSR_VM_HSAVE_PA`` and ``EFER`` values
after L2 returns.  On the current Xen host that restore triggers a guest
``#GP`` on the HSAVE MSR write and leaves Xen with a fresh anonymous
``--pscd`` domain.

Suggested Xen-Side Checks
-------------------------

* Verify how L2 SVM opcodes are decoded and classified before falling into the
  MMIO/emulation failure path.
* Check whether nested-SVM handling is incorrectly routing guest SVM opcodes to
  generic emulator/MMIO logic.
* Check whether the L2 ``VMRUN`` intercept path returns a clean
  ``VMEXIT_VMRUN`` to L1 or loses the guest before the nested exit is surfaced.
* Correlate the guest RIP in Xen logs with the reproducer disassembly to see
  whether failure happens on the guest opcode itself or earlier during nested
  state preparation.