.ps2_ee_vu0

  ;; Input qword: [source address, output address, unused, unused].
  ;; Copy x/y, rotate the complete source, then round-trip source z through
  ;; MTIR/MFIR into the copied vector's w lane.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         lq.xyzw vf01, 0(vi01)

  nop                         move.xy vf02, vf01
  nop                         mr32.xyzw vf03, vf01
  nop                         mtir vi03, vf01z
  nop                         mfir.w vf02, vi03

  nop                         sqi.xyzw vf02, (vi02++)
  nop                         sqi.xyzw vf03, (vi02++)

  nop[E]                      nop
  nop                         nop
