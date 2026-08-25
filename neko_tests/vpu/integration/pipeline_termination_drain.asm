.ps2_ee_vu0

  ;; Input qword: [source address, output address, one, unused].
  ;; Termination drains overlapping FMAC, IALU, LSU, ACC, and LOI work.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)
  nop                         lq.xyzw vf01, 0(vi01)

  mula.xyzw ACC, vf01, vf01   iadd vi04, vi03, vi03
  add[E].xyzw vf03, vf01, vf01  sqi.xyzw vf01, (vi02++)

  ;; E delay pair:
  ;;   addi[I].xyzw vf04, vf01, I   LOI 10.0
  ;; The ADDi observes old I while LOI remains active during pipeline drain.
  dc32 0x41200000, 0x81e00922
