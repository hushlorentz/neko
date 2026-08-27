.ps2_ee_vu0

  ;; Input qword: [source address, output address, unused, unused].
  ;; Source qword supplies the RINIT seed and RXOR operand.
  ;; Output: [initial R, initial R, next R, XORed R].

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         lq.xy vf01, 0(vi01)

  nop                         rinit R, vf01x
  nop                         rget.xy vf02, R
  nop                         rnext.z vf02, R
  nop                         rxor R, vf01y
  nop                         rget.w vf02, R

  nop                         sqi.xyzw vf02, (vi02++)

  nop[E]                      nop
  nop                         nop
