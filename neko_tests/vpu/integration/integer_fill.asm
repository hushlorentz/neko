.ps2_ee_vu0

  ;; Input qword: [count, first value, increment, unused].
  ;; Output qwords replace the input and contain the same integer in every lane.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi05, 0(vi00)
  nop                         iadd vi03, vi00, vi00

loop:
  nop                         mfir.xyzw vf01, vi02
  nop                         sqi.xyzw vf01, (vi03++)
  nop                         iadd vi02, vi02, vi05
  nop                         isubiu vi01, vi01, 1
  nop                         ibne vi01, vi00, loop

  ;; Preserve the next output address as an observable delay-slot result.
  nop                         iadd vi04, vi03, vi00

  nop[E]                      nop
  nop                         nop
