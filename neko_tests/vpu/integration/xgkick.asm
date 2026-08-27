.ps2_ee_vu1

  ;; Input qword: [PATH1 qword address, output address, unused, unused].
  ;; The host-side handler contract verifies the handoff; this fixture verifies
  ;; decoded VU1 execution, source hazards, pipeline timing, and drain behavior.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         xgkick vi01
  nop                         iaddi vi03, vi00, 7
  nop                         mfir.x vf01, vi03
  nop                         sqi.xyzw vf01, (vi02++)

  nop[E]                      nop
  nop                         nop
