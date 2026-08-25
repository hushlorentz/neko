.ps2_ee_vu0

  ;; Input qword: [left address, right address, output address, unused].
  ;; Outputs record immediate ACC forwarding and lane-specific VF hazards.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)

  nop                         lq.xyzw vf01, 0(vi01)
  nop                         lq.xyzw vf02, 0(vi02)

  ;; MADD consumes the preceding MULA result before ACC is architectural.
  mula.xyzw ACC, vf01, vf02   nop
  madd.xyzw vf03, vf01, vf02  nop

  ;; Reading untouched y does not stall on the pending vf04.x write.
  ;; Reading x in the following instruction does.
  add.x vf04, vf01, vf02      nop
  add.y vf05, vf04, vf01      nop
  add.x vf06, vf04, vf01      nop

  nop                         sqi.xyzw vf03, (vi03++)
  nop                         sqi.xyzw vf05, (vi03++)
  nop                         sqi.xyzw vf06, (vi03++)

  nop[E]                      nop
  nop                         nop
