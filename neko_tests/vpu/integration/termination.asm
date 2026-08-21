.ps2_ee_vu0

branch_end_entry:
  ;; Input qword 0: [one, two, output address, unused].
  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)
  nop                         mfir.xyzw vf01, vi01
  nop                         sqi.xyzw vf01, (vi03++)

  ;; E on the branch executes the sequential delay slot, redirects the TPC,
  ;; and terminates without executing the target.
  nop[E]                      ibne vi01, vi02, branch_end_target
  nop                         iadd vi04, vi01, vi02
  nop                         iadd vi05, vi01, vi02
branch_end_target:
  nop                         iadd vi05, vi01, vi02
  nop                         nop

delay_end_entry:
  ;; Input qword 1: [one, two, output address, unused].
  nop                         ilw.x vi01, 1(vi00)
  nop                         ilw.y vi02, 1(vi00)
  nop                         ilw.z vi03, 1(vi00)
  nop                         mfir.xyzw vf01, vi02
  nop                         sqi.xyzw vf01, (vi03++)

  ;; E in the branch delay slot makes the branch target the E delay pair.
  nop                         ibne vi01, vi02, delay_end_target
  nop[E]                      nop
  nop                         iadd vi05, vi01, vi02
  nop                         iadd vi05, vi01, vi02
delay_end_target:
  nop                         iadd vi04, vi01, vi02

  ;; Termination must occur before this pair is fetched.
  nop                         iadd vi06, vi01, vi02
