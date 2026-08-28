.ps2_ee_vu1

  ;; Input qword 0: [0.5, 1.0, unused, unused].
  ;; Input qword 1: [output address, unused, unused, unused].
  ;; Output: [ESIN(0.5), EEXP(1.0), 0, 0].

  nop                         lq.xy vf01, 0(vi00)
  nop                         ilw.x vi01, 1(vi00)
  nop                         esin P, vf01x
  nop                         waitp
  nop                         mfp.x vf02, P
  nop                         eexp P, vf01y
  nop                         waitp
  nop                         mfp.y vf02, P
  nop                         sqi.xyzw vf02, (vi01++)

  nop[E]                      nop
  nop                         nop
