.ps2_ee_vu0

  ;; Input qword: [left address, right address, output address, unused].
  ;; The addition produces status flags 0x00c3.
  ;; Output: [FSAND result, FSEQ result, FSOR result, sticky FSAND result].

  nop                         ilw.x vi10, 0(vi00)
  nop                         ilw.y vi11, 0(vi00)
  nop                         ilw.z vi12, 0(vi00)
  nop                         lq.xyzw vf01, 0(vi10)
  nop                         lq.xyzw vf02, 0(vi11)

  add.xyzw vf03, vf01, vf02   nop
  nop                         nop
  nop                         nop
  nop                         nop

  nop                         fsand vi02, 0x082
  nop                         fseq vi03, 0x0c3
  nop                         fsor vi04, 0x004
  nop                         fsset 0xfc0
  nop                         nop
  nop                         nop
  nop                         nop
  nop                         fsand vi05, 0xfc0

  nop                         mfir.x vf04, vi02
  nop                         mfir.y vf04, vi03
  nop                         mfir.z vf04, vi04
  nop                         mfir.w vf04, vi05
  nop                         sqi.xyzw vf04, (vi12++)

  nop[E]                      nop
  nop                         nop
