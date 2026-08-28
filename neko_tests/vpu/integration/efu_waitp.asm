.ps2_ee_vu1

  ;; Input qword 0: source vector [1, 2, 3, 4].
  ;; Input qword 1: [output address, unused, unused, unused].
  ;; P is preloaded by the harness.
  ;; Output qword 0: [P before WAITP, ESUM result, 0, 0].
  ;; Output qword 1: [ESADD result, 0, 0, 0].

  nop                         lq.xyzw vf01, 0(vi00)
  nop                         ilw.x vi01, 1(vi00)
  nop                         esum P, vf01
  nop                         mfp.x vf02, P
  nop                         waitp
  nop                         mfp.y vf02, P
  nop                         sqi.xyzw vf02, (vi01++)
  nop                         esadd P, vf01
  nop                         waitp
  nop                         mfp.x vf03, P
  nop                         sqi.xyzw vf03, (vi01++)

  nop[E]                      nop
  nop                         nop
