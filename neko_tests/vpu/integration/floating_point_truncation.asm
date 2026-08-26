.ps2_ee_vu0

  ;; Input qword: [left address, right address, output address, unused].
  ;; Outputs: truncated add, subtract, and multiply results.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)

  nop                         lq.xyzw vf01, 0(vi01)
  nop                         lq.xyzw vf02, 0(vi02)

  add.xyzw vf03, vf01, vf02   nop
  sub.xyzw vf04, vf01, vf02   nop
  mul.xyzw vf05, vf01, vf02   nop

  nop                         sqi.xyzw vf03, (vi03++)
  nop                         sqi.xyzw vf04, (vi03++)
  nop                         sqi.xyzw vf05, (vi03++)

  nop[E]                      nop
  nop                         nop
