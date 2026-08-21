.ps2_ee_vu0

  ;; Input qword: [left address, right address, output address, unused].
  ;; Results: product, doubled product through ACC, and product plus left.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)

  nop                         lq.xyzw vf01, 0(vi01)
  nop                         lq.xyzw vf02, 0(vi02)

  mul.xyzw vf03, vf01, vf02   nop
  mula.xyzw ACC, vf01, vf02   nop
  madd.xyzw vf04, vf01, vf02  nop
  sub.xyzw vf05, vf04, vf03   nop
  add.xyzw vf06, vf05, vf01   nop

  nop                         sqi.xyzw vf03, (vi03++)
  nop                         sqi.xyzw vf04, (vi03++)
  nop                         sqi.xyzw vf06, (vi03++)

  nop[E]                      nop
  nop                         nop
