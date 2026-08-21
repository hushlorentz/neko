.ps2_ee_vu0

  ;; Input qword: [subroutine byte address, output qword address,
  ;;               finish byte address, one].
  ;; Output qword: [call signature, return signature, jump signature, link].

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)
  nop                         ilw.w vi04, 0(vi00)

  nop                         iadd vi05, vi00, vi00
  nop                         iadd vi06, vi00, vi00
  nop                         iadd vi07, vi00, vi00

  ;; The delay slot must observe the old zero value of the link register.
  nop                         jalr vi15, vi01
  nop                         iadd vi05, vi15, vi04

returned:
  nop                         iadd vi06, vi06, vi04
  nop                         jr vi03
  nop                         iadd vi07, vi07, vi04

  ;; JR must skip this wrong-path update.
  nop                         iadd vi06, vi06, vi04

subroutine:
  nop                         iadd vi05, vi05, vi04
  nop                         jr vi15
  nop                         iadd vi05, vi05, vi04

  ;; The return JR must not fall through.
  nop                         iadd vi06, vi06, vi04

finish:
  nop                         mfir.x vf01, vi05
  nop                         mfir.y vf01, vi06
  nop                         mfir.z vf01, vi07
  nop                         mfir.w vf01, vi15
  nop                         sqi.xyzw vf01, (vi02++)

  nop[E]                      nop
  nop                         nop
