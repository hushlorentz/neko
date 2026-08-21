.ps2_ee_vu0

  ;; Input qword 0: [count, input address, output address, one].
  ;; Qwords 1-4: scale, bias, lower bound, upper bound.
  ;; For each input qword:
  ;;   output = min(max(input * scale + bias, lower), upper)

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)
  nop                         ilw.w vi04, 0(vi00)

  nop                         lq.xyzw vf10, 1(vi00)
  nop                         lq.xyzw vf11, 2(vi00)
  nop                         lq.xyzw vf12, 3(vi00)
  nop                         lq.xyzw vf13, 4(vi00)
  nop                         iadd vi05, vi00, vi00

loop:
  nop                         lq.xyzw vf01, 0(vi02)
  mul.xyzw vf02, vf01, vf10   iadd vi02, vi02, vi04
  add.xyzw vf02, vf02, vf11   iadd vi05, vi05, vi04
  max.xyzw vf02, vf02, vf12   nop
  mini.xyzw vf02, vf02, vf13  isubiu vi01, vi01, 1
  nop                         sqi.xyzw vf02, (vi03++)
  nop                         ibne vi01, vi00, loop
  nop                         nop

  nop[E]                      nop
  nop                         nop
