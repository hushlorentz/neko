.ps2_ee_vu0

  ;; Input qword: [source address, output address, integer sentinel, unused].
  ;; Load every source lane independently, replace y/z through a mixed MFIR,
  ;; then store individual and mixed masks over preinitialized output qwords.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)

  nop                         lq.x vf01, 0(vi01)
  nop                         lq.y vf01, 0(vi01)
  nop                         lq.z vf01, 0(vi01)
  nop                         lq.w vf01, 0(vi01)

  nop                         mfir.yz vf01, vi03
  nop                         mfir.xyzw vf02, vi00
  add.xw vf03, vf01, vf02     nop

  nop                         sqi.x vf01, (vi02++)
  nop                         sqi.y vf01, (vi02++)
  nop                         sqi.z vf01, (vi02++)
  nop                         sqi.w vf01, (vi02++)
  nop                         sqi.xw vf03, (vi02++)

  nop[E]                      nop
  nop                         nop
