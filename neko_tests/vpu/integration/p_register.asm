.ps2_ee_vu1

  ;; Input qword: [output address, unused, unused, unused].
  ;; P is preloaded by the harness.
  ;; Output: the raw P value broadcast to all four lanes.

  nop                         ilw.x vi01, 0(vi00)
  nop                         mfp.xyzw vf01, P
  nop                         sqi.xyzw vf01, (vi01++)

  nop[E]                      nop
  nop                         nop
