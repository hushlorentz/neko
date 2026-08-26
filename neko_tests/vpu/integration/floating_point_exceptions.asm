.ps2_ee_vu0

  ;; Input qword: [left address, right address, output address, unused].
  ;; The four multiply lanes cover flushed input zero, signed zero,
  ;; exponent overflow to MAX, and exponent underflow to zero.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)

  nop                         lq.xyzw vf01, 0(vi01)
  nop                         lq.xyzw vf02, 0(vi02)

  mul.xyzw vf03, vf01, vf02   nop
  nop                         sqi.xyzw vf03, (vi03++)

  nop[E]                      nop
  nop                         nop
