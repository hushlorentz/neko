.ps2_ee_vu1

  ;; Input qword 0: source vector [2, 0, 0, -8].
  ;; Input qword 1: [output address, unused, unused, unused].
  ;; Output: [length, reciprocal(w), reciprocal square sum,
  ;;          reciprocal length].

  nop                         lq.xyzw vf01, 0(vi00)
  nop                         ilw.x vi01, 1(vi00)
  nop                         eleng P, vf01
  nop                         waitp
  nop                         mfp.x vf02, P
  nop                         ercpr P, vf01w
  nop                         waitp
  nop                         mfp.y vf02, P
  nop                         ersadd P, vf01
  nop                         waitp
  nop                         mfp.z vf02, P
  nop                         erleng P, vf01
  nop                         waitp
  nop                         mfp.w vf02, P
  nop                         sqi.xyzw vf02, (vi01++)

  nop[E]                      nop
  nop                         nop
