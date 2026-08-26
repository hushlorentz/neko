.ps2_ee_vu0

  ;; Input qword: [floating-point base, integer address, output address, unused].
  ;; Four floating-point qwords provide boundaries for FTOI0/4/12/15.
  ;; One integer qword is converted by ITOF0/4/12/15.
  ;; Outputs are the four FTOI results followed by the four ITOF results.

  nop                           ilw.x vi01, 0(vi00)
  nop                           ilw.y vi02, 0(vi00)
  nop                           ilw.z vi03, 0(vi00)

  nop                           lq.xyzw vf01, 0(vi01)
  nop                           lq.xyzw vf02, 1(vi01)
  nop                           lq.xyzw vf03, 2(vi01)
  nop                           lq.xyzw vf04, 3(vi01)
  nop                           lq.xyzw vf05, 0(vi02)

  ;; Seed overflow flags; conversion instructions must leave them unchanged.
  mul.xyzw vf06, vf01, vf01     nop

  ftoi0.xyzw vf10, vf01         nop
  ftoi4.xyzw vf11, vf02         nop
  ftoi12.xyzw vf12, vf03        nop
  ftoi15.xyzw vf13, vf04        nop
  itof0.xyzw vf14, vf05         nop
  itof4.xyzw vf15, vf05         nop
  itof12.xyzw vf16, vf05        nop
  itof15.xyzw vf17, vf05        nop

  nop                           sqi.xyzw vf10, (vi03++)
  nop                           sqi.xyzw vf11, (vi03++)
  nop                           sqi.xyzw vf12, (vi03++)
  nop                           sqi.xyzw vf13, (vi03++)
  nop                           sqi.xyzw vf14, (vi03++)
  nop                           sqi.xyzw vf15, (vi03++)
  nop                           sqi.xyzw vf16, (vi03++)
  nop                           sqi.xyzw vf17, (vi03++)

  nop[E]                        nop
  nop                           nop
