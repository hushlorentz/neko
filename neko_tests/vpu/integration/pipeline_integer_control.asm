.ps2_ee_vu0

  ;; Input qword: [base address, offset, output address, unused].
  ;; IALU results feed both an LSU address and the following branch.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)

  nop                         iadd vi04, vi01, vi02
  nop                         lq.xyzw vf01, 0(vi04)

  nop                         iadd vi05, vi01, vi02
  nop                         ibne vi05, vi02, taken
  nop                         iadd vi06, vi04, vi05

  ;; A taken branch must skip this signature.
  nop                         mfir.xyzw vf01, vi00

taken:
  nop                         mfir.w vf01, vi06
  nop                         sqi.xyzw vf01, (vi03++)

  nop[E]                      nop
  nop                         nop
