.ps2_ee_vu0

  ;; Input qword: [left address, right address, output address, one].
  ;; Six consecutive pairs perform upper vector work and a dependent lower
  ;; integer chain. Results are followed by the final integer signature.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)
  nop                         ilw.w vi04, 0(vi00)

  nop                         lq.xyzw vf01, 0(vi01)
  nop                         lq.xyzw vf02, 0(vi02)

  ;; Isolate the sustained issue window from the preceding LQ latency.
  nop                         nop
  nop                         nop
  nop                         nop
  nop                         nop
  nop                         nop
  nop                         nop

  add.xyzw vf03, vf01, vf02   iadd vi05, vi04, vi04
  sub.xyzw vf04, vf01, vf02   iadd vi06, vi05, vi04
  mul.xyzw vf05, vf01, vf02   iadd vi07, vi06, vi04
  max.xyzw vf06, vf01, vf02   iadd vi08, vi07, vi04
  mini.xyzw vf07, vf01, vf02  iadd vi09, vi08, vi04
  add.xyzw vf08, vf01, vf02   iadd vi10, vi09, vi04

  nop                         mfir.xyzw vf09, vi10
  nop                         sqi.xyzw vf03, (vi03++)
  nop                         sqi.xyzw vf04, (vi03++)
  nop                         sqi.xyzw vf05, (vi03++)
  nop                         sqi.xyzw vf06, (vi03++)
  nop                         sqi.xyzw vf07, (vi03++)
  nop                         sqi.xyzw vf08, (vi03++)
  nop                         sqi.xyzw vf09, (vi03++)

  nop[E]                      nop
  nop                         nop
