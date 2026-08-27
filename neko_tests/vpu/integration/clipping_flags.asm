.ps2_ee_vu0

  ;; Input qword: [output address, unused, unused, unused].
  ;; Output: [FCGET low 12 bits, FCAND result, FCEQ result, FCOR result].

  nop                         ilw.x vi10, 0(vi00)
  nop                         fcset 0x123456
  nop                         nop
  nop                         nop
  nop                         nop

  nop                         fcget vi02
  nop                         fcand vi01, 0x000010
  nop                         iadd vi03, vi01, vi00
  nop                         fceq vi01, 0x123456
  nop                         iadd vi04, vi01, vi00
  nop                         fcor vi01, 0xedcba9
  nop                         iadd vi05, vi01, vi00

  nop                         mfir.x vf01, vi02
  nop                         mfir.y vf01, vi03
  nop                         mfir.z vf01, vi04
  nop                         mfir.w vf01, vi05
  nop                         sqi.xyzw vf01, (vi10++)

  nop[E]                      nop
  nop                         nop
