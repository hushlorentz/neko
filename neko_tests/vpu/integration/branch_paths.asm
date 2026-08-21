.ps2_ee_vu0

  ;; Input qword: [one, two, output address, unused].
  ;; Output qword records forward, fallthrough, backward-loop, and final
  ;; delay-slot signatures.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)

  nop                         iadd vi04, vi00, vi00
  nop                         iadd vi05, vi00, vi00
  nop                         iadd vi07, vi00, vi00
  nop                         iadd vi08, vi00, vi00

  ;; Taken forward: skip the following wrong-path signature update.
  nop                         ibne vi01, vi02, forward_taken
  nop                         iadd vi04, vi04, vi01
  nop                         iadd vi05, vi05, vi02

forward_taken:
  ;; Untaken forward: execute the fallthrough path.
  nop                         ibne vi01, vi01, unexpected
  nop                         iadd vi04, vi04, vi02
  nop                         iadd vi05, vi05, vi01

  nop                         iadd vi06, vi02, vi00

backward_loop:
  nop                         isubiu vi06, vi06, 1
  nop                         ibne vi06, vi00, backward_loop
  nop                         iadd vi07, vi07, vi01

  ;; Taken forward: skip the unexpected target after executing its delay slot.
  nop                         ibne vi01, vi00, results
  nop                         iadd vi08, vi08, vi01

unexpected:
  nop                         iadd vi05, vi05, vi02

results:
  nop                         mfir.x vf01, vi04
  nop                         mfir.y vf01, vi05
  nop                         mfir.z vf01, vi07
  nop                         mfir.w vf01, vi08
  nop                         sqi.xyzw vf01, (vi03++)

  nop[E]                      nop
  nop                         nop
