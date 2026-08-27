.ps2_ee_vu0

  ;; Input qword: [left address, right address, output address, unused].
  ;; The addition produces MAC flags 0x0084 (negative x and zero y).
  ;; Output: [FMAND result, FMEQ result, FMOR result, zero].

  nop                         ilw.x vi10, 0(vi00)
  nop                         ilw.y vi11, 0(vi00)
  nop                         ilw.z vi12, 0(vi00)
  nop                         lq.xyzw vf01, 0(vi10)
  nop                         lq.xyzw vf02, 0(vi11)

  add.xyzw vf03, vf01, vf02   nop
  nop                         nop
  nop                         nop
  nop                         nop

  nop                         iaddiu vi01, vi00, 0x008c
  nop                         fmand vi02, vi01
  nop                         iaddiu vi01, vi00, 0x0084
  nop                         fmeq vi03, vi01
  nop                         iaddiu vi01, vi00, 0x0003
  nop                         fmor vi04, vi01

  nop                         mfir.x vf04, vi02
  nop                         mfir.y vf04, vi03
  nop                         mfir.z vf04, vi04
  nop                         sqi.xyzw vf04, (vi12++)

  nop[E]                      nop
  nop                         nop
