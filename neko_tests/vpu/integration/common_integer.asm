.ps2_ee_vu0

  ;; Input qword: [left operand, right operand, output address, unused].
  ;; The dependent IALU chain proves one-cycle bypass and 16-bit wrapping.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi08, 0(vi00)

  nop                         iaddi vi03, vi01, -16
  nop                         iaddiu vi04, vi03, 0x7fff
  nop                         iand vi05, vi04, vi02
  nop                         ior vi06, vi05, vi01
  nop                         isub vi07, vi06, vi02

  nop                         mfir.x vf01, vi03
  nop                         mfir.y vf01, vi04
  nop                         mfir.z vf01, vi05
  nop                         mfir.w vf01, vi06
  nop                         sqi.xyzw vf01, (vi08++)

  nop                         mfir.x vf02, vi07
  nop                         sqi.xyzw vf02, (vi08++)

  nop[E]                      nop
  nop                         nop
