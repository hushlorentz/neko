.ps2_ee_vu0

  ;; Input qword: [source base, output base, unused, unused].
  ;; Five source qwords cover truncating division, signed zero, DIV
  ;; exceptions, SQRT absolute-value behavior, and RSQRT.
  ;; The fifth qword is 1.0 in every lane so MULq copies raw Q, including -0.

  nop                           ilw.x vi01, 0(vi00)
  nop                           ilw.y vi02, 0(vi00)

  nop                           lq.xyzw vf01, 0(vi01)
  nop                           lq.xyzw vf02, 1(vi01)
  nop                           lq.xyzw vf03, 2(vi01)
  nop                           lq.xyzw vf04, 3(vi01)
  nop                           lq.xyzw vf05, 4(vi01)

  nop                           div Q, vf01x, vf01y
  nop                           waitq
  mulq.x vf10, vf05, Q          nop

  nop                           div Q, vf02x, vf02y
  nop                           waitq
  mulq.x vf11, vf05, Q          nop

  nop                           div Q, vf03x, vf03y
  nop                           waitq
  mulq.x vf12, vf05, Q          nop

  nop                           sqrt Q, vf02z
  nop                           waitq
  mulq.x vf13, vf05, Q          nop

  nop                           sqrt Q, vf02w
  nop                           waitq
  mulq.x vf14, vf05, Q          nop

  nop                           rsqrt Q, vf01z, vf04x
  nop                           waitq
  mulq.x vf15, vf05, Q          nop

  nop                           rsqrt Q, vf01z, vf04y
  nop                           waitq
  mulq.x vf16, vf05, Q          nop

  nop                           div Q, vf02x, vf04z
  nop                           waitq
  mulq.x vf17, vf05, Q          nop

  nop                           sqi.xyzw vf10, (vi02++)
  nop                           sqi.xyzw vf11, (vi02++)
  nop                           sqi.xyzw vf12, (vi02++)
  nop                           sqi.xyzw vf13, (vi02++)
  nop                           sqi.xyzw vf14, (vi02++)
  nop                           sqi.xyzw vf15, (vi02++)
  nop                           sqi.xyzw vf16, (vi02++)
  nop                           sqi.xyzw vf17, (vi02++)

  nop[E]                        nop
  nop                           nop
