.ps2_ee_vu1

  ;; Input qword 0: [1.0, 0.5, 0.25, 1.0].
  ;; Input qword 1: [output address, unused, unused, unused].
  ;; Output: [atan(w), atan(y/x), atan(z/x), 0].

  nop                         lq.xyzw vf01, 0(vi00)
  nop                         ilw.x vi01, 1(vi00)
  nop                         eatan P, vf01w
  nop                         waitp
  nop                         mfp.x vf02, P
  nop                         eatanxy P, vf01
  nop                         waitp
  nop                         mfp.y vf02, P
  nop                         eatanxz P, vf01
  nop                         waitp
  nop                         mfp.z vf02, P
  nop                         sqi.xyzw vf02, (vi01++)

  nop[E]                      nop
  nop                         nop
