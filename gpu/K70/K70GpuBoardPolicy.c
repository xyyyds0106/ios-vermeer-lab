/* SPDX-License-Identifier: MIT */
#include "K70GpuBoardPolicy.h"
/* Fixed K70 resource contract reviewed against saved live.dtb and pinned
 * MiCode 53ef33ea board drivers; each value is compared with THIS boot.
 * These constants are guards, never a replacement for the supplied FDT. */
struct rule {const char *path,*name;const uint8_t *value;unsigned bytes;};
static const struct rule rules[]={
    {"/","model",(const uint8_t *)"\x51\x75\x61\x6c\x63\x6f\x6d\x6d\x20\x54\x65\x63\x68\x6e\x6f\x6c\x6f\x67\x69\x65\x73\x2c\x20\x49\x6e\x63\x2e\x20\x38\x35\x35\x30\x20\x76\x65\x72\x6d\x65\x65\x72\x00",41},
    {"/","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x6b\x61\x6c\x61\x6d\x61\x2d\x6d\x74\x70\x00\x71\x63\x6f\x6d\x2c\x6b\x61\x6c\x61\x6d\x61\x00\x71\x63\x6f\x6d\x2c\x6d\x74\x70\x00",37},
    {"/","#address-cells",(const uint8_t *)"\x00\x00\x00\x02",4},
    {"/","#size-cells",(const uint8_t *)"\x00\x00\x00\x02",4},
    {"/soc","#address-cells",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc","#size-cells",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc","ranges",(const uint8_t *)"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\xff",16},
    {"/reserved-memory","#address-cells",(const uint8_t *)"\x00\x00\x00\x02",4},
    {"/reserved-memory","#size-cells",(const uint8_t *)"\x00\x00\x00\x02",4},
    {"/reserved-memory","ranges",(const uint8_t *)"",0},
    {"/reserved-memory/aop_cmd_db_region@81c60000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x63\x6d\x64\x2d\x64\x62\x00",12},
    {"/reserved-memory/aop_cmd_db_region@81c60000","reg",(const uint8_t *)"\x00\x00\x00\x00\x81\xc6\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00",16},
    {"/reserved-memory/aop_cmd_db_region@81c60000","no-map",(const uint8_t *)"",0},
    {"/reserved-memory/gpu_micro_code_region@9b09a000","reg",(const uint8_t *)"\x00\x00\x00\x00\x9b\x09\xa0\x00\x00\x00\x00\x00\x00\x00\x20\x00",16},
    {"/reserved-memory/gpu_micro_code_region@9b09a000","no-map",(const uint8_t *)"",0},
    {"/reserved-memory/gpu_micro_code_region@9b09a000","phandle",(const uint8_t *)"\x00\x00\x02\xcf",4},
    {"/soc/apps_rsc@17a00000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x72\x70\x6d\x68\x2d\x72\x73\x63\x00",14},
    {"/soc/apps_rsc@17a00000","reg",(const uint8_t *)"\x17\xa0\x00\x00\x00\x01\x00\x00\x17\xa1\x00\x00\x00\x01\x00\x00\x17\xa2\x00\x00\x00\x01\x00\x00",24},
    {"/soc/apps_rsc@17a00000","reg-names",(const uint8_t *)"\x64\x72\x76\x2d\x30\x00\x64\x72\x76\x2d\x31\x00\x64\x72\x76\x2d\x32\x00",18},
    {"/soc/apps_rsc@17a00000","qcom,drv-count",(const uint8_t *)"\x00\x00\x00\x03",4},
    {"/soc/apps_rsc@17a00000","interrupts",(const uint8_t *)"\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x00\x05\x00\x00\x00\x04",36},
    {"/soc/apps_rsc@17a00000/drv@2","qcom,drv-id",(const uint8_t *)"\x00\x00\x00\x02",4},
    {"/soc/apps_rsc@17a00000/drv@2","qcom,tcs-offset",(const uint8_t *)"\x00\x00\x0d\x00",4},
    {"/soc/apps_rsc@17a00000/drv@2/channel@0","qcom,tcs-config",(const uint8_t *)"\x00\x00\x00\x02\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x02\x00\x00\x00\x01\x00\x00\x00\x02\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x04\x00\x00\x00\x01",40},
    {"/soc/interrupt-controller@17100000","compatible",(const uint8_t *)"\x61\x72\x6d\x2c\x67\x69\x63\x2d\x76\x33\x00",11},
    {"/soc/interrupt-controller@17100000","reg",(const uint8_t *)"\x17\x10\x00\x00\x00\x01\x00\x00\x17\x18\x00\x00\x00\x20\x00\x00",16},
    {"/soc/interrupt-controller@17100000","phandle",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc/interrupt-controller@17100000","interrupt-controller",(const uint8_t *)"",0},
    {"/soc/interrupt-controller@17100000","#interrupt-cells",(const uint8_t *)"\x00\x00\x00\x03",4},
    {"/soc/qcom,kgsl-3d0@3d00000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x61\x64\x72\x65\x6e\x6f\x2d\x67\x70\x75\x2d\x67\x65\x6e\x37\x2d\x32\x2d\x31\x00\x71\x63\x6f\x6d\x2c\x6b\x67\x73\x6c\x2d\x33\x64\x30\x00",39},
    {"/soc/qcom,kgsl-3d0@3d00000","reg",(const uint8_t *)"\x03\xd0\x00\x00\x00\x04\x00\x00\x03\xd6\x10\x00\x00\x00\x08\x00\x03\xd5\x00\x00\x00\x01\x00\x00\x03\xd8\xb0\x00\x00\x00\x20\x00\x03\xd9\xe0\x00\x00\x00\x10\x00\x10\x90\x00\x00\x00\x08\x00\x00\x10\x04\x80\x00\x00\x00\x80\x00\x10\xb0\x50\x00\x00\x00\x10\x00",64},
    {"/soc/qcom,kgsl-3d0@3d00000","reg-names",(const uint8_t *)"\x6b\x67\x73\x6c\x5f\x33\x64\x30\x5f\x72\x65\x67\x5f\x6d\x65\x6d\x6f\x72\x79\x00\x63\x78\x5f\x64\x62\x67\x63\x00\x72\x73\x63\x63\x00\x69\x73\x65\x6e\x73\x65\x5f\x63\x6e\x74\x6c\x00\x63\x78\x5f\x6d\x69\x73\x63\x00\x71\x64\x73\x73\x5f\x67\x66\x78\x00\x71\x64\x73\x73\x5f\x65\x74\x72\x00\x71\x64\x73\x73\x5f\x74\x6d\x63\x00",80},
    {"/soc/qcom,kgsl-3d0@3d00000","clocks",(const uint8_t *)"\x00\x00\x00\x37\x00\x00\x00\x20\x00\x00\x00\x37\x00\x00\x00\x21\x00\x00\x00\x4e\x00\x00\x00\x00\x00\x00\x00\x5c\x00\x00\x00\x00",32},
    {"/soc/qcom,kgsl-3d0@3d00000","clock-names",(const uint8_t *)"\x67\x63\x63\x5f\x67\x70\x75\x5f\x6d\x65\x6d\x6e\x6f\x63\x5f\x67\x66\x78\x00\x67\x63\x63\x5f\x67\x70\x75\x5f\x73\x6e\x6f\x63\x5f\x64\x76\x6d\x5f\x67\x66\x78\x00\x67\x70\x75\x5f\x63\x63\x5f\x61\x68\x62\x00\x61\x70\x62\x5f\x70\x63\x6c\x6b\x00",60},
    {"/soc/qcom,kgsl-3d0@3d00000","interrupts",(const uint8_t *)"\x00\x00\x00\x00\x00\x00\x01\x2c\x00\x00\x00\x04",12},
    {"/soc/qcom,kgsl-3d0@3d00000","phandle",(const uint8_t *)"\x00\x00\x00\xcd",4},
    {"/soc/qcom,kgsl-3d0@3d00000","qcom,bus-table-cnoc",(const uint8_t *)"\x00\x00\x00\x00\x00\x00\x00\x64",8},
    {"/soc/qcom,kgsl-3d0@3d00000","qcom,ubwc-mode",(const uint8_t *)"\x00\x00\x00\x04",4},
    {"/soc/qcom,kgsl-3d0@3d00000","qcom,min-access-length",(const uint8_t *)"\x00\x00\x00\x20",4},
    {"/soc/qcom,kgsl-3d0@3d00000/zap-shader","memory-region",(const uint8_t *)"\x00\x00\x02\xcf",4},
    {"/soc/qcom,kgsl-3d0@3d00000/qcom,gpu-pwrlevel-bins","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x67\x70\x75\x2d\x70\x77\x72\x6c\x65\x76\x65\x6c\x2d\x62\x69\x6e\x73\x00",23},
    {"/soc/qcom,kgsl-3d0@3d00000/qcom,gpu-pwrlevel-bins","#address-cells",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc/qcom,kgsl-3d0@3d00000/qcom,gpu-pwrlevel-bins","#size-cells",(const uint8_t *)"\x00\x00\x00\x00",4},
    {"/soc/kgsl-smmu@3da0000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x71\x73\x6d\x6d\x75\x2d\x76\x35\x30\x30\x00\x71\x63\x6f\x6d\x2c\x61\x64\x72\x65\x6e\x6f\x2d\x73\x6d\x6d\x75\x00",33},
    {"/soc/kgsl-smmu@3da0000","reg",(const uint8_t *)"\x03\xda\x00\x00\x00\x04\x00\x00\x03\xde\x60\x00\x00\x00\x00\x40",16},
    {"/soc/kgsl-smmu@3da0000","reg-names",(const uint8_t *)"\x62\x61\x73\x65\x00\x74\x63\x75\x2d\x62\x61\x73\x65\x00",14},
    {"/soc/kgsl-smmu@3da0000","#iommu-cells",(const uint8_t *)"\x00\x00\x00\x02",4},
    {"/soc/kgsl-smmu@3da0000","#address-cells",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc/kgsl-smmu@3da0000","#size-cells",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc/kgsl-smmu@3da0000","ranges",(const uint8_t *)"",0},
    {"/soc/kgsl-smmu@3da0000","dma-coherent",(const uint8_t *)"",0},
    {"/soc/kgsl-smmu@3da0000","qcom,actlr",(const uint8_t *)"\x00\x00\x00\x00\x00\x00\x03\xff\x00\x00\x03\x2b",12},
    {"/soc/kgsl-smmu@3da0000","clocks",(const uint8_t *)"\x00\x00\x00\x4e\x00\x00\x00\x0f\x00\x00\x00\x37\x00\x00\x00\x20\x00\x00\x00\x37\x00\x00\x00\x21\x00\x00\x00\x4e\x00\x00\x00\x00",32},
    {"/soc/kgsl-smmu@3da0000","clock-names",(const uint8_t *)"\x67\x70\x75\x5f\x63\x63\x5f\x68\x6c\x6f\x73\x31\x5f\x76\x6f\x74\x65\x5f\x67\x70\x75\x5f\x73\x6d\x6d\x75\x00\x67\x63\x63\x5f\x67\x70\x75\x5f\x6d\x65\x6d\x6e\x6f\x63\x5f\x67\x66\x78\x00\x67\x63\x63\x5f\x67\x70\x75\x5f\x73\x6e\x6f\x63\x5f\x64\x76\x6d\x5f\x67\x66\x78\x00\x67\x70\x75\x5f\x63\x63\x5f\x61\x68\x62\x00",78},
    {"/soc/kgsl-smmu@3da0000","interrupts",(const uint8_t *)"\x00\x00\x00\x00\x00\x00\x02\xa1\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xa5\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xa6\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xa7\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xa8\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xa9\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xaa\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xab\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xac\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xad\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xae\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xaf\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x01\xa6\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x01\xdc\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x3e\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x3f\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x40\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x41\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x93\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x95\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x98\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x99\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x9a\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x9c\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\x9d\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x02\xbb\x00\x00\x00\x04",312},
    {"/soc/kgsl-smmu@3da0000","phandle",(const uint8_t *)"\x00\x00\x00\xdd",4},
    {"/soc/kgsl-smmu@3da0000","vdd-supply",(const uint8_t *)"\x00\x00\x00\xd9",4},
    {"/soc/kgsl-smmu@3da0000/gpu_qtb@3de8000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x71\x73\x6d\x6d\x75\x76\x35\x30\x30\x2d\x74\x62\x75\x00\x71\x63\x6f\x6d\x2c\x71\x74\x62\x35\x30\x30\x00",31},
    {"/soc/kgsl-smmu@3da0000/gpu_qtb@3de8000","reg",(const uint8_t *)"\x03\xde\x80\x00\x00\x00\x10\x00\x03\xde\xc0\x00\x00\x00\x10\x00",16},
    {"/soc/kgsl-smmu@3da0000/gpu_qtb@3de8000","reg-names",(const uint8_t *)"\x62\x61\x73\x65\x00\x64\x65\x62\x75\x67\x63\x68\x61\x69\x6e\x2d\x62\x61\x73\x65\x00",21},
    {"/soc/kgsl-smmu@3da0000/gpu_qtb@3de8000","qcom,stream-id-range",(const uint8_t *)"\x00\x00\x00\x00\x00\x00\x04\x00",8},
    {"/soc/kgsl-smmu@3da0000/gpu_qtb@3de8000","qcom,iova-width",(const uint8_t *)"\x00\x00\x00\x31",4},
    {"/soc/qcom,kgsl-iommu@3da0000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x6b\x67\x73\x6c\x2d\x73\x6d\x6d\x75\x2d\x76\x32\x00",18},
    {"/soc/qcom,kgsl-iommu@3da0000","reg",(const uint8_t *)"\x03\xda\x00\x00\x00\x04\x00\x00",8},
    {"/soc/qcom,kgsl-iommu@3da0000","vddcx-supply",(const uint8_t *)"\x00\x00\x00\xd9",4},
    {"/soc/qcom,kgsl-iommu@3da0000/gfx3d_user","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x73\x6d\x6d\x75\x2d\x6b\x67\x73\x6c\x2d\x63\x62\x00",18},
    {"/soc/qcom,kgsl-iommu@3da0000/gfx3d_user","iommus",(const uint8_t *)"\x00\x00\x00\xdd\x00\x00\x00\x00\x00\x00\x00\x00",12},
    {"/soc/qcom,kgsl-iommu@3da0000/gfx3d_lpac","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x73\x6d\x6d\x75\x2d\x6b\x67\x73\x6c\x2d\x63\x62\x00",18},
    {"/soc/qcom,kgsl-iommu@3da0000/gfx3d_lpac","iommus",(const uint8_t *)"\x00\x00\x00\xdd\x00\x00\x00\x01\x00\x00\x00\x00",12},
    {"/soc/qcom,kgsl-iommu@3da0000/gfx3d_secure","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x73\x6d\x6d\x75\x2d\x6b\x67\x73\x6c\x2d\x63\x62\x00",18},
    {"/soc/qcom,kgsl-iommu@3da0000/gfx3d_secure","iommus",(const uint8_t *)"\x00\x00\x00\xdd\x00\x00\x00\x02\x00\x00\x00\x00",12},
    {"/soc/qcom,gmu@3d69000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x67\x65\x6e\x37\x2d\x67\x6d\x75\x00",14},
    {"/soc/qcom,gmu@3d69000","reg",(const uint8_t *)"\x03\xd6\x80\x00\x00\x03\x70\x00\x0b\x28\x00\x00\x00\x01\x00\x00\x03\xd4\x00\x00\x00\x01\x00\x00",24},
    {"/soc/qcom,gmu@3d69000","reg-names",(const uint8_t *)"\x67\x6d\x75\x00\x67\x6d\x75\x5f\x70\x64\x63\x00\x67\x6d\x75\x5f\x61\x6f\x5f\x62\x6c\x6b\x5f\x64\x65\x63\x30\x00",28},
    {"/soc/qcom,gmu@3d69000","clocks",(const uint8_t *)"\x00\x00\x00\x4e\x00\x00\x00\x03\x00\x00\x00\x4e\x00\x00\x00\x05\x00\x00\x00\x37\x00\x00\x00\x0d\x00\x00\x00\x37\x00\x00\x00\x20\x00\x00\x00\x4e\x00\x00\x00\x00\x00\x00\x00\x4e\x00\x00\x00\x12",48},
    {"/soc/qcom,gmu@3d69000","clock-names",(const uint8_t *)"\x67\x6d\x75\x5f\x63\x6c\x6b\x00\x63\x78\x6f\x5f\x63\x6c\x6b\x00\x61\x78\x69\x5f\x63\x6c\x6b\x00\x6d\x65\x6d\x6e\x6f\x63\x5f\x63\x6c\x6b\x00\x61\x68\x62\x5f\x63\x6c\x6b\x00\x68\x75\x62\x5f\x63\x6c\x6b\x00",51},
    {"/soc/qcom,gmu@3d69000","interrupts",(const uint8_t *)"\x00\x00\x00\x00\x00\x00\x01\x30\x00\x00\x00\x04\x00\x00\x00\x00\x00\x00\x01\x31\x00\x00\x00\x04",24},
    {"/soc/qcom,gmu@3d69000","iommus",(const uint8_t *)"\x00\x00\x00\xdd\x00\x00\x00\x05\x00\x00\x00\x00",12},
    {"/soc/qcom,gmu@3d69000","vdd-supply",(const uint8_t *)"\x00\x00\x04\xc7",4},
    {"/soc/qcom,gmu@3d69000","vddcx-supply",(const uint8_t *)"\x00\x00\x00\xd9",4},
    {"/soc/qcom,gmu@3d69000","mboxes",(const uint8_t *)"\x00\x00\x00\x60\x00\x00\x00\x00",8},
    {"/soc/clock-controller@3d90000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x6b\x61\x6c\x61\x6d\x61\x2d\x67\x70\x75\x63\x63\x00\x73\x79\x73\x63\x6f\x6e\x00",25},
    {"/soc/clock-controller@3d90000","reg",(const uint8_t *)"\x03\xd9\x00\x00\x00\x00\xa0\x00",8},
    {"/soc/clock-controller@3d90000","#clock-cells",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc/clock-controller@3d90000","phandle",(const uint8_t *)"\x00\x00\x00\x4e",4},
    {"/soc/clock-controller@3d90000","clocks",(const uint8_t *)"\x00\x00\x00\x38\x00\x00\x00\x00\x00\x00\x00\x37\x00\x00\x00\x1e\x00\x00\x00\x37\x00\x00\x00\x1f",24},
    {"/soc/clock-controller@3d90000","clock-names",(const uint8_t *)"\x62\x69\x5f\x74\x63\x78\x6f\x00\x67\x70\x6c\x6c\x30\x5f\x6f\x75\x74\x5f\x6d\x61\x69\x6e\x00\x67\x70\x6c\x6c\x30\x5f\x6f\x75\x74\x5f\x6d\x61\x69\x6e\x5f\x64\x69\x76\x00",42},
    {"/soc/clock-controller@3d90000","vdd_cx-supply",(const uint8_t *)"\x00\x00\x00\x23",4},
    {"/soc/clock-controller@3d90000","vdd_mx-supply",(const uint8_t *)"\x00\x00\x00\x26",4},
    {"/soc/clock-controller@3d90000","vdd_mxc-supply",(const uint8_t *)"\x00\x00\x00\x27",4},
    {"/soc/clock-controller@100000","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x6b\x61\x6c\x61\x6d\x61\x2d\x67\x63\x63\x2d\x76\x32\x00\x73\x79\x73\x63\x6f\x6e\x00",26},
    {"/soc/clock-controller@100000","reg",(const uint8_t *)"\x00\x10\x00\x00\x00\x1f\x42\x00",8},
    {"/soc/clock-controller@100000","#clock-cells",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc/clock-controller@100000","phandle",(const uint8_t *)"\x00\x00\x00\x37",4},
    {"/soc/clock-controller@100000","vdd_cx-supply",(const uint8_t *)"\x00\x00\x00\x23",4},
    {"/soc/clock-controller@100000","vdd_mxa-supply",(const uint8_t *)"\x00\x00\x00\x26",4},
    {"/soc/qcom,gdsc@3d99108","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x67\x64\x73\x63\x00",10},
    {"/soc/qcom,gdsc@3d99108","reg",(const uint8_t *)"\x03\xd9\x91\x08\x00\x00\x00\x04",8},
    {"/soc/qcom,gdsc@3d99108","parent-supply",(const uint8_t *)"\x00\x00\x00\x23",4},
    {"/soc/qcom,gdsc@3d99108","phandle",(const uint8_t *)"\x00\x00\x00\xd9",4},
    {"/soc/qcom,gdsc@3d99108","hw-ctrl-addr",(const uint8_t *)"\x00\x00\x02\xbd",4},
    {"/soc/qcom,gdsc@3d9905c","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x67\x64\x73\x63\x00",10},
    {"/soc/qcom,gdsc@3d9905c","reg",(const uint8_t *)"\x03\xd9\x90\x5c\x00\x00\x00\x04",8},
    {"/soc/qcom,gdsc@3d9905c","parent-supply",(const uint8_t *)"\x00\x00\x02\xc2",4},
    {"/soc/qcom,gdsc@3d9905c","phandle",(const uint8_t *)"\x00\x00\x04\xc7",4},
    {"/soc/qcom,gdsc@3d9905c","domain-addr",(const uint8_t *)"\x00\x00\x02\xbe",4},
    {"/soc/qcom,gdsc@3d9905c","sw-reset",(const uint8_t *)"\x00\x00\x02\xbf\x00\x00\x02\xc0\x00\x00\x02\xc1",12},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-gfxlvl","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x72\x70\x6d\x68\x2d\x61\x72\x63\x2d\x72\x65\x67\x75\x6c\x61\x74\x6f\x72\x00",24},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-gfxlvl","qcom,resource-name",(const uint8_t *)"\x67\x66\x78\x2e\x6c\x76\x6c\x00",8},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-cxlvl","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x72\x70\x6d\x68\x2d\x61\x72\x63\x2d\x72\x65\x67\x75\x6c\x61\x74\x6f\x72\x00",24},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-cxlvl","qcom,resource-name",(const uint8_t *)"\x63\x78\x2e\x6c\x76\x6c\x00",7},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxlvl","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x72\x70\x6d\x68\x2d\x61\x72\x63\x2d\x72\x65\x67\x75\x6c\x61\x74\x6f\x72\x00",24},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxlvl","qcom,resource-name",(const uint8_t *)"\x6d\x78\x2e\x6c\x76\x6c\x00",7},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x72\x70\x6d\x68\x2d\x61\x72\x63\x2d\x72\x65\x67\x75\x6c\x61\x74\x6f\x72\x00",24},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl","qcom,resource-name",(const uint8_t *)"\x6d\x78\x63\x2e\x6c\x76\x6c\x00",8},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-gfxlvl/regulator-pm-v6d-s5-level","phandle",(const uint8_t *)"\x00\x00\x00\x28",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-gfxlvl/regulator-pm-v6d-s5-level","regulator-min-microvolt",(const uint8_t *)"\x00\x00\x00\x10",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-gfxlvl/regulator-pm-v6d-s5-level","regulator-max-microvolt",(const uint8_t *)"\x00\x00\xff\xff",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-cxlvl/regulator-pm-v6e-s6-level","phandle",(const uint8_t *)"\x00\x00\x00\x23",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-cxlvl/regulator-pm-v6e-s6-level","regulator-min-microvolt",(const uint8_t *)"\x00\x00\x00\x10",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-cxlvl/regulator-pm-v6e-s6-level","regulator-max-microvolt",(const uint8_t *)"\x00\x00\xff\xff",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxlvl/regulator-pm-v6e-s3-level","phandle",(const uint8_t *)"\x00\x00\x00\x26",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxlvl/regulator-pm-v6e-s3-level","regulator-min-microvolt",(const uint8_t *)"\x00\x00\x00\x10",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxlvl/regulator-pm-v6e-s3-level","regulator-max-microvolt",(const uint8_t *)"\x00\x00\xff\xff",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl/regulator-pm-v8-s5-level","phandle",(const uint8_t *)"\x00\x00\x00\x27",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl/regulator-pm-v8-s5-level","regulator-min-microvolt",(const uint8_t *)"\x00\x00\x00\x10",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl/regulator-pm-v8-s5-level","regulator-max-microvolt",(const uint8_t *)"\x00\x00\xff\xff",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl/regulator-pm-v8-s5-gfx-voter-level","phandle",(const uint8_t *)"\x00\x00\x02\xc2",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl/regulator-pm-v8-s5-gfx-voter-level","regulator-min-microvolt",(const uint8_t *)"\x00\x00\x00\x30",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl/regulator-pm-v8-s5-gfx-voter-level","regulator-max-microvolt",(const uint8_t *)"\x00\x00\xff\xff",4},
    {"/soc/apps_rsc@17a00000/drv@2/rpmh-regulator-mxclvl/regulator-pm-v8-s5-gfx-voter-level","pm_v8_s5_gfx_voter_level-parent-supply",(const uint8_t *)"\x00\x00\x00\x28",4},
    {"/soc/syscon@3d9953c","phandle",(const uint8_t *)"\x00\x00\x02\xbd",4},
    {"/soc/syscon@3d9953c","reg",(const uint8_t *)"\x03\xd9\x95\x3c\x00\x00\x00\x04",8},
    {"/soc/syscon@3d9953c","compatible",(const uint8_t *)"\x73\x79\x73\x63\x6f\x6e\x00",7},
    {"/soc/syscon@3d99504","phandle",(const uint8_t *)"\x00\x00\x02\xbe",4},
    {"/soc/syscon@3d99504","reg",(const uint8_t *)"\x03\xd9\x95\x04\x00\x00\x00\x04",8},
    {"/soc/syscon@3d99504","compatible",(const uint8_t *)"\x73\x79\x73\x63\x6f\x6e\x00",7},
    {"/soc/syscon@3d99058","phandle",(const uint8_t *)"\x00\x00\x02\xbf",4},
    {"/soc/syscon@3d99058","reg",(const uint8_t *)"\x03\xd9\x90\x58\x00\x00\x00\x04",8},
    {"/soc/syscon@3d99058","compatible",(const uint8_t *)"\x73\x79\x73\x63\x6f\x6e\x00",7},
    {"/soc/syscon@3d99358","phandle",(const uint8_t *)"\x00\x00\x02\xc0",4},
    {"/soc/syscon@3d99358","reg",(const uint8_t *)"\x03\xd9\x93\x58\x00\x00\x00\x04",8},
    {"/soc/syscon@3d99358","compatible",(const uint8_t *)"\x73\x79\x73\x63\x6f\x6e\x00",7},
    {"/soc/syscon@3d9958c","phandle",(const uint8_t *)"\x00\x00\x02\xc1",4},
    {"/soc/syscon@3d9958c","reg",(const uint8_t *)"\x03\xd9\x95\x8c\x00\x00\x00\x04",8},
    {"/soc/syscon@3d9958c","compatible",(const uint8_t *)"\x73\x79\x73\x63\x6f\x6e\x00",7},
    {"/soc/qcom,qmp-aop","compatible",(const uint8_t *)"\x71\x63\x6f\x6d\x2c\x71\x6d\x70\x2d\x6d\x62\x6f\x78\x00",14},
    {"/soc/qcom,qmp-aop","qcom,qmp",(const uint8_t *)"\x00\x00\x00\x5c",4},
    {"/soc/qcom,qmp-aop","#mbox-cells",(const uint8_t *)"\x00\x00\x00\x01",4},
    {"/soc/qcom,qmp-aop","phandle",(const uint8_t *)"\x00\x00\x00\x60",4},
};
#define RULES (sizeof(rules)/sizeof(rules[0]))
static uint32_t be32(const uint8_t *p)
{return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const uint8_t *p){return (uint64_t)be32(p)<<32|be32(p+4);}
static size_t length(const char *p){size_t n=0;while(p[n])++n;return n;}
static int equal(const uint8_t *p,size_t n,const char *q)
{size_t i=0;while(i<n && q[i] && p[i]==(uint8_t)q[i])++i;return i==n && !q[i];}
static int bytes_equal(const uint8_t *a,const uint8_t *b,size_t n)
{for(size_t i=0;i<n;++i)if(a[i]!=b[i])return 0;return 1;}
static int prefix(const char *p,const char *q)
{size_t i=0;while(q[i] && p[i]==q[i])++i;return !q[i];}
static int ancestor(const char *p,const char *child)
{size_t n=length(p);return prefix(child,p) && (n==1 || !child[n] || child[n]=='/');}
static int disjoint(const void *a,size_t an,const void *b,size_t bn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x>=an:x-y>=bn;}
struct view {const uint8_t *p;size_t bytes;};
struct level {uint32_t field[6];unsigned seen,node;};
static int capture(struct view *out,const uint8_t *p,size_t bytes)
{if(out->p)return K70BoardContract;out->p=p;out->bytes=bytes;return 0;}
/* Returns -1 for unrelated node, -2 for an unsupported direct child. */
static int level_index(const char *path,const char *bin)
{
    if(!prefix(path,bin) || path[length(bin)]!='/')return -1;
    const char *p=path+length(bin)+1;
    if(!prefix(p,"qcom,gpu-pwrlevel@"))return -2;
    p+=length("qcom,gpu-pwrlevel@");unsigned n;
    if(p[0]>='0' && p[0]<='9')n=(unsigned)(p[0]-'0');
    else if(p[0]>='a' && p[0]<='e')n=(unsigned)(p[0]-'a')+10;
    else return -2;
    return p[1]?-2:(int)n;
}
static int parse(const uint8_t *f,size_t available,const struct K70GpuBoardQualification *q,
    struct K70GpuBoardPolicy *out)
{
    if(available<40 || be32(f)!=0xd00dfeed)return K70BoardFdt;
    size_t total=be32(f+4),at=be32(f+8),strings=be32(f+12),reserve=be32(f+16),sn=be32(f+32),size=be32(f+36);
    if(total<40 || total>available || total>0x200000 || at<40 || (at&3) || at>total || size>total-at ||
       strings<40 || strings>total || sn>total-strings || (at<strings+sn && strings<at+size) ||
       reserve<40 || (reserve&7) || reserve>total || be32(f+20)!=17 || be32(f+24)>17)return K70BoardFdt;
    size_t end_reserve=reserve;
    for(unsigned i=0;;++i) {
        if(i>4096 || total-end_reserve<16)return K70BoardFdt;
        uint64_t address=be64(f+end_reserve),bytes=be64(f+end_reserve+8);end_reserve+=16;
        if(!address && !bytes)break;
        if(!bytes || address>UINT64_MAX-bytes)return K70BoardFdt;
    }
    if((reserve<at+size && at<end_reserve) || (reserve<strings+sn && strings<end_reserve))return K70BoardFdt;
    const char *gpu="/soc/qcom,kgsl-3d0@3d00000",*gmu="/soc/qcom,gmu@3d69000";
    const char *bins[2]={"/soc/qcom,kgsl-3d0@3d00000/qcom,gpu-pwrlevel-bins/qcom,gpu-pwrlevels-0",
        "/soc/qcom,kgsl-3d0@3d00000/qcom,gpu-pwrlevel-bins/qcom,gpu-pwrlevels-1"};
    const char *bin=bins[q->speed_bin];
    uint8_t seen[RULES]={0},nodes[RULES]={0},status_seen[64]={0},child_seen[64]={0};
    struct view ddr={0},gmu_freq={0},sku={0},initial={0};struct level levels[15]={0};
    char path[512];size_t lengths[64],used=0,limit=at+size;
    unsigned depth=0,roots=0,node_count=0,properties=0,bin_seen=0,bin_cells=0,ended=0;
    while(at<limit) {
        if(limit-at<4)return K70BoardFdt;
        uint32_t token=be32(f+at);at+=4;
        if(token==1) {
            size_t start=at;while(at<limit && f[at])++at;
            if(at==limit || depth>=64 || at-start>255 || (depth && at==start) || ++node_count>16384 ||
               (!depth && (roots++ || at!=start)))return K70BoardFdt;
            if(depth)child_seen[depth-1]=1;
            lengths[depth]=used;status_seen[depth]=child_seen[depth]=0;
            if(!depth){path[0]='/';used=1;}
            else {
                if(depth>1){if(used>=sizeof(path)-1)return K70BoardFdt;path[used++]='/';}
                if(at-start>=sizeof(path)-used)return K70BoardFdt;
                for(size_t j=start;j<at;++j){if(f[j]=='/')return K70BoardFdt;path[used++]=(char)f[j];}
            }
            path[used]=0;++depth;at=(at+4)&~(size_t)3;
            for(size_t i=0;i<RULES;++i)if(equal((const uint8_t *)path,used,rules[i].path)) {
                if(nodes[i])return K70BoardContract;nodes[i]=1;
            }
            if(equal((const uint8_t *)path,used,bin)){if(bin_seen++)return K70BoardContract;}
            int index=level_index(path,bin);
            if(index==-2)return K70BoardOpp;
            if(index>=0){if(levels[index].node++)return K70BoardOpp;}
        } else if(token==2) {
            if(!depth)return K70BoardFdt;used=lengths[--depth];path[used]=0;
        } else if(token==3) {
            if(!depth || child_seen[depth-1] || limit-at<8 || ++properties>65536)return K70BoardFdt;
            size_t bytes=be32(f+at),no=be32(f+at+4);at+=8;
            if(bytes>limit-at || no>=sn)return K70BoardFdt;
            size_t nn=0;while(nn<sn-no && f[strings+no+nn])++nn;
            if(nn==sn-no || !nn || nn>255)return K70BoardFdt;
            const uint8_t *name=f+strings+no,*value=f+at;
            if(equal(name,nn,"status")) {
                int relevant=ancestor(path,bin) || ancestor(bin,path);
                for(size_t i=0;i<RULES && !relevant;++i)relevant=ancestor(path,rules[i].path);
                if(relevant && (status_seen[depth-1]++ ||
                    !((bytes==3 && bytes_equal(value,(const uint8_t *)"ok",3)) ||
                      (bytes==5 && bytes_equal(value,(const uint8_t *)"okay",5)))))return K70BoardStatus;
            }
            if(equal(name,nn,"phandle") || equal(name,nn,"linux,phandle")) {
                if(bytes!=4)return K70BoardContract;
                for(size_t i=0;i<RULES;++i)if(equal((const uint8_t *)rules[i].name,length(rules[i].name),"phandle") &&
                    be32(value)==be32(rules[i].value) && !equal((const uint8_t *)path,used,rules[i].path))return K70BoardContract;
            }
            for(size_t i=0;i<RULES;++i)if(equal((const uint8_t *)path,used,rules[i].path) && equal(name,nn,rules[i].name)) {
                if(seen[i]++ || bytes!=rules[i].bytes || !bytes_equal(value,rules[i].value,bytes))return K70BoardContract;
            }
            int status=0;
            if(equal((const uint8_t *)path,used,gpu)) {
                if(equal(name,nn,"qcom,bus-table-ddr"))status=capture(&ddr,value,bytes);
                if(equal(name,nn,"qcom,gpu-pwrlevel-mask") || equal(name,nn,"qcom,speed-bin"))return K70BoardOpp;
            } else if(equal((const uint8_t *)path,used,gmu) && equal(name,nn,"qcom,gmu-freq-table"))
                status=capture(&gmu_freq,value,bytes);
            else if(equal((const uint8_t *)path,used,bin)) {
                if(equal(name,nn,"qcom,sku-codes"))status=capture(&sku,value,bytes);
                if(equal(name,nn,"qcom,initial-pwrlevel"))status=capture(&initial,value,bytes);
                if(equal(name,nn,"#address-cells")) {
                    if((bin_cells&1) || bytes!=4 || be32(value)!=1)return K70BoardOpp;bin_cells|=1;
                }
                if(equal(name,nn,"#size-cells")) {
                    if((bin_cells&2) || bytes!=4 || be32(value))return K70BoardOpp;bin_cells|=2;
                }
            }
            int index=level_index(path,bin);
            if(index>=0) {
                static const char *fields[]={"reg","qcom,gpu-freq","qcom,level","qcom,bus-freq","qcom,bus-min","qcom,bus-max"};
                for(unsigned i=0;i<6;++i)if(equal(name,nn,fields[i])) {
                    if(bytes!=4 || (levels[index].seen&(1u<<i)))return K70BoardOpp;
                    levels[index].seen|=1u<<i;levels[index].field[i]=be32(value);
                }
            }
            if(status)return status;at=(at+bytes+3)&~(size_t)3;
        } else if(token==9) {
            if(depth || roots!=1 || at!=limit)return K70BoardFdt;ended=1;break;
        } else if(token!=4)return K70BoardFdt;
        if(at>limit)return K70BoardFdt;
    }
    if(!ended)return K70BoardFdt;
    for(size_t i=0;i<RULES;++i)if(!seen[i])return K70BoardContract;
    if(bin_seen!=1 || bin_cells!=3 || !sku.p || !sku.bytes || (sku.bytes&3) || sku.bytes>256)return K70BoardSku;
    unsigned found=0;
    for(size_t i=0;i<sku.bytes;i+=4){uint32_t code=be32(sku.p+i);
        for(size_t j=0;j<i;j+=4)if(code==be32(sku.p+j))return K70BoardSku;
        found+=code==q->sku_code;}
    if(found!=1)return K70BoardSku;
    struct K70GpuBoardPolicy result={0};struct K70GpuPowerPolicy *p=&result.power;
    if(!ddr.p || ddr.bytes<8 || ddr.bytes>64 || (ddr.bytes&3) || be32(ddr.p))return K70BoardOpp;
    p->bandwidth_count=(unsigned)(ddr.bytes/4);
    for(unsigned i=0;i<p->bandwidth_count;++i){p->bandwidth_kbps[i]=be32(ddr.p+4*i);
        if(i && p->bandwidth_kbps[i]<=p->bandwidth_kbps[i-1])return K70BoardOpp;}
    if(!gmu_freq.p || gmu_freq.bytes<8 || gmu_freq.bytes>24 || (gmu_freq.bytes&7))return K70BoardOpp;
    p->gmu_count=(unsigned)(gmu_freq.bytes/8)+1;
    for(unsigned i=1;i<p->gmu_count;++i) {
        p->gmu_hz[i]=be32(gmu_freq.p+8*(i-1));p->gmu_level[i]=be32(gmu_freq.p+8*(i-1)+4);
        if(!p->gmu_level[i] || p->gmu_level[i]>65535 || p->gmu_hz[i]%1000 || p->gmu_hz[i]<=p->gmu_hz[i-1] ||
           p->gmu_level[i]<p->gmu_level[i-1])return K70BoardOpp;
        if(p->gmu_hz[i]==220000000)result.request.cx_level=p->gmu_level[i];
    }
    unsigned count=0;
    while(count<15 && levels[count].node)++count;
    if(!count || !initial.p || initial.bytes!=4 || be32(initial.p)>=count)return K70BoardOpp;
    for(unsigned i=count;i<15;++i)if(levels[i].node)return K70BoardOpp;
    p->gpu_count=count+1;
    for(unsigned i=0;i<count;++i) {
        const struct level *v=&levels[count-1-i];unsigned n=i+1;
        if(v->seen!=63 || v->field[0]!=count-1-i || !v->field[2] || v->field[2]>65535 ||
           v->field[1]%1000 || v->field[1]<=p->gpu_hz[n-1] || v->field[2]<p->gpu_level[n-1] ||
           !v->field[3] || !v->field[4] || v->field[4]>v->field[3] || v->field[3]>v->field[5] ||
           v->field[5]>=p->bandwidth_count)return K70BoardOpp;
        p->gpu_hz[n]=v->field[1];p->gpu_level[n]=v->field[2];
        if(v->field[1]==220000000){result.request.gfx_level=v->field[2];result.request.bandwidth_kbps=p->bandwidth_kbps[v->field[3]];}
    }
    if(!result.request.gfx_level || !result.request.cx_level || !result.request.bandwidth_kbps)return K70BoardOpp;
    /* MiCode53ef gpucc-kalama.c: GMU220MHz and PLL1's440MHz require
     * VDD_LOWER_D1 on MX. vdd-level.h maps it to LOW_SVS_D1, and the pinned
     * rpmh-regulator-levels.h defines56. This is a clock floor, not a measured
     * voltage or an assertion that CX64 and MX56 use the same aux index.
     * The actual cmd-db-dependent index merge belongs to PowerAcquire. */
    p->boot_mx_level=result.request.mx_level=56;
    result.request.gpu_hz=result.request.gmu_hz=220000000;
    /* Pinned public Linux A740 catalog: SH0/MC0 connection widths, and ACV
     * fixed perfmode bit3 above16,500,000KB/s. Addresses/unit/width/VCD are
     * deliberately absent here: the provider obtains them from live cmd-db. */
    p->bcm[0].buswidth=16;p->bcm[1].buswidth=4;p->bcm[2].fixed=1;
    p->bcm[2].perfmode=8;p->bcm[2].perfmode_kbps=16500000;
    result.chip_id=q->soc.gpu_chip_id;result.speed_bin=q->speed_bin;result.sku_code=q->sku_code;
    result.smmu_actlr=0x32b;result.zap_base=UINT64_C(0x9b09a000);result.zap_bytes=8192;result.mxc_supply_present=1;
    p->qualified=1;*out=result;return 0;
}
int K70GpuBoardPolicyBuild(const uint8_t *f,size_t bytes,const struct K70GpuBoardQualification *q,
    struct K70GpuBoardPolicy *out)
{
    if(!f || !q || !out || !disjoint(f,bytes,out,sizeof(*out)) || !disjoint(q,sizeof(*q),out,sizeof(*out)))
        return K70BoardArgument;
    if(q->socinfo_qualified!=1 || q->soc.format<16 || q->soc.format>19 || !q->soc.soc_id ||
       q->soc.soc_id==UINT32_MAX || !q->soc.gpu_chip_id || q->soc.gpu_chip_id==UINT32_MAX)return K70BoardChip;
    if(q->speed_bin_qualified!=1 || q->speed_bin>1)return K70BoardSpeedBin;
    return parse(f,bytes,q,out);
}
