.ps2_ee_vu1

  ;; Input qword 0: source vector [4, 9, -16, 25].
  ;; Input qword 1: [output address, unused, unused, unused].
  ;; Output: [sqrt(abs(z)), 1 / sqrt(abs(w)), 0, 0].

  nop                         lq.xyzw vf01, 0(vi00)
  nop                         ilw.x vi01, 1(vi00)
  nop                         esqrt P, vf01z
  nop                         waitp
  nop                         mfp.x vf02, P
  nop                         ersqrt P, vf01w
  nop                         waitp
  nop                         mfp.y vf02, P
  nop                         sqi.xyzw vf02, (vi01++)

  nop[E]                      nop
  nop                         nop
