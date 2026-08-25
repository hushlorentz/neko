.ps2_ee_vu0

  ;; Input qword: [output address, unused, unused, unused].
  ;; Source VF02 and the old I value are initialized by the test harness.

  nop                         ilw.x vi01, 0(vi00)

  ;; naken_asm does not expose the LOI pseudo-instruction. This pair is:
  ;;   addi[I].x vf01, vf02, I   LOI 10.0
  ;; The paired ADDi samples old I; the following ADDi samples 10.0.
  dc32 0x41200000, 0x81001062
  addi.x vf03, vf02, I         nop

  nop                          sqi.xyzw vf01, (vi01++)
  nop                          sqi.xyzw vf03, (vi01++)

  nop[E]                       nop
  nop                          nop
