.ps2_ee_vu0

  ;; Input qword 0 contains the two address registers. The following qwords
  ;; contain raw lane patterns used to distinguish all addressing modes.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)

  nop                         ilwr.z vi03, (vi02)
  nop                         lqd.xz vf01, (--vi01)
  nop                         lqi.yw vf02, (vi02++)

  nop                         iswr.y vi03, (vi01)
  nop                         sq.xy vf01, 3(vi02)
  nop                         isw.xw vi03, 4(vi02)
  nop[E]                      sqd.zw vf02, (--vi02)
  nop                         nop
