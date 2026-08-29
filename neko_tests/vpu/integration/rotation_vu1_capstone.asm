.ps2_ee_vu1

  ;; Input:
  ;;   qword 0: [point count, first point, point stride, packet address]
  ;;   qwords 1-3: three rows of a rotation matrix
  ;;   qword 4: [projection scale, translate x, depth base, translate y]
  ;;   packet data begins at the configured packet address
  ;;
  ;; Each point is transformed by the matrix, projected, converted to GS
  ;; fixed-point coordinates, and written back before the packet is kicked.

  nop                         ilw.x vi01, 0(vi00)
  nop                         ilw.y vi02, 0(vi00)
  nop                         ilw.z vi03, 0(vi00)
  nop                         ilw.w vi04, 0(vi00)
  nop                         lq.xyzw vf11, 1(vi00)
  nop                         lq.xyzw vf12, 2(vi00)
  nop                         lq.xyzw vf13, 3(vi00)
  nop                         lq.xyzw vf20, 4(vi00)

next_point:
  nop                         lq.xyzw vf10, 0(vi02)
  mul.xyzw vf30, vf11, vf10   nop
  nop                         esum P, vf30
  nop                         waitp
  mul.xyzw vf30, vf12, vf10   mfp.x vf31, P
  nop                         esum P, vf30
  nop                         waitp
  mul.xyzw vf30, vf13, vf10   mfp.y vf31, P
  nop                         esum P, vf30
  nop                         waitp
  nop                         mfp.z vf31, P

  nop                         ercpr P, vf31z
  nop                         waitp
  nop                         mfp.xy vf22, P
  mulx.xy vf31, vf31, vf20    nop
  mul.xy vf31, vf31, vf22     isubiu vi01, vi01, 1
  addy.x vf31, vf31, vf20     nop
  addw.y vf31, vf31, vf20     nop
  subz.z vf31, vf20, vf31     nop
  ftoi4.xy vf31, vf31         nop
  ftoi0.z vf31, vf31          nop
  nop                         sq.xyzw vf31, 0(vi02)
  nop                         iadd vi02, vi02, vi03
  nop                         ibne vi01, vi00, next_point
  nop                         nop

  nop                         xgkick vi04
  nop                         nop
  nop[E]                      nop
  nop                         nop
