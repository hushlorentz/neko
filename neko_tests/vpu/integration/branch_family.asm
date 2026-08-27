.ps2_ee_vu0

  ;; Input qword: [positive, zero, negative, output address].
  ;; Every branch is taken, so only the eight delay-slot increments contribute
  ;; to VI04. BAL also records its post-delay return address.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)
  nop                         ilw.w vi10, 0(vi00)

  nop                         b after_b
  nop                         iaddi vi04, vi04, 1
  nop                         iaddi vi04, vi04, 8

after_b:
  nop                         bal vi15, after_bal
  nop                         iaddi vi04, vi04, 1
  nop                         iaddi vi04, vi04, 8

after_bal:
  nop                         ibeq vi02, vi02, after_ibeq
  nop                         iaddi vi04, vi04, 1
  nop                         iaddi vi04, vi04, 8

after_ibeq:
  nop                         ibne vi01, vi02, after_ibne
  nop                         iaddi vi04, vi04, 1
  nop                         iaddi vi04, vi04, 8

after_ibne:
  nop                         ibgez vi02, after_ibgez
  nop                         iaddi vi04, vi04, 1
  nop                         iaddi vi04, vi04, 8

after_ibgez:
  nop                         ibgtz vi01, after_ibgtz
  nop                         iaddi vi04, vi04, 1
  nop                         iaddi vi04, vi04, 8

after_ibgtz:
  nop                         iblez vi02, after_iblez
  nop                         iaddi vi04, vi04, 1
  nop                         iaddi vi04, vi04, 8

after_iblez:
  nop                         ibltz vi03, results
  nop                         iaddi vi04, vi04, 1
  nop                         iaddi vi04, vi04, 8

results:
  nop                         mfir.x vf01, vi04
  nop                         mfir.y vf01, vi15
  nop                         mfir.z vf01, vi03
  nop                         mfir.w vf01, vi01
  nop                         sqi.xyzw vf01, (vi10++)

  nop[E]                      nop
  nop                         nop
