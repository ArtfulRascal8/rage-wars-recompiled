# Review of the 18 additional 496-closure deltas

These deltas reconcile the accepted closure-specific C with clean generation from the complete 3,751-function symbol inventory. The checked source-built generator replayed all 496 closure bodies to the frozen aggregate output hash exactly (`e208df091c715458f65a887238aeaf449ef3cb451a5de3d8074c85f7c36201df`), and every accepted body here matches that closure-specific output. In the full inventory, each changed line renders the same numeric guest target as `LOOKUP_FUNC(address)` instead of the closure-specific named function symbol. The resulting exact patches preserve the accepted body bytes; behavioral equivalence is not inferred from matching addresses.

Each row includes the original ROM extent/hash and both generated-body hashes. The JSON companion stores each complete unified diff as the exact patch preimage/result record.

| Function | Guest | ROM | Size | Rewritten calls |
|---|---:|---:|---:|---|
| `rom_closure_00204870_r00005470` | `0x00204870` | `0x005470` | `0x1F8` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×1 |
| `rom_closure_00204BD0_r000057D0` | `0x00204BD0` | `0x0057D0` | `0x64` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×1 |
| `rom_closure_00204DE0_r000059E0` | `0x00204DE0` | `0x0059E0` | `0x1C` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×1 |
| `rom_closure_00204E2C_r00005A2C` | `0x00204E2C` | `0x005A2C` | `0x1C` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×1 |
| `rom_closure_002050A0_r00005CA0` | `0x002050A0` | `0x005CA0` | `0x16C` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×1 |
| `rom_closure_0020524C_r00005E4C` | `0x0020524C` | `0x005E4C` | `0x78` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×2 |
| `rom_closure_00205694_r00006294` | `0x00205694` | `0x006294` | `0x3C` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×1 |
| `rom_closure_00230AE4_r000316E4` | `0x00230AE4` | `0x0316E4` | `0xC4` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×1 |
| `rom_closure_00231F4C_r00032B4C` | `0x00231F4C` | `0x032B4C` | `0x25C` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×2 |
| `rom_closure_002336FC_r000342FC` | `0x002336FC` | `0x0342FC` | `0x70` | `resident_wave14_func_00214178(rdram, ctx);` → `LOOKUP_FUNC(0x00214178)` ×1 |
| `rom_closure_0041CEB0_r00180EB0` | `0x0041CEB0` | `0x180EB0` | `0x2F4` | `trace_func_0040ECB0_r00172CB0(rdram, ctx);` → `LOOKUP_FUNC(0x0040ECB0)` ×11 |
| `rom_closure_0041D718_r00181718` | `0x0041D718` | `0x181718` | `0x2B8` | `trace_func_0040ECB0_r00172CB0(rdram, ctx);` → `LOOKUP_FUNC(0x0040ECB0)` ×7 |
| `rom_closure_0041DACC_r00181ACC` | `0x0041DACC` | `0x181ACC` | `0xD4` | `trace_func_0040ECB0_r00172CB0(rdram, ctx);` → `LOOKUP_FUNC(0x0040ECB0)` ×1 |
| `rom_closure_00436488_r0019A488` | `0x00436488` | `0x19A488` | `0xEC` | `resident_wave14_external_00449B20(rdram, ctx);` → `LOOKUP_FUNC(0x00449B20)` ×1 |
| `rom_closure_004378F8_r0019B8F8` | `0x004378F8` | `0x19B8F8` | `0xA0` | `trace_func_0040ECB0_r00172CB0(rdram, ctx);` → `LOOKUP_FUNC(0x0040ECB0)` ×1 |
| `rom_closure_0043E5F4_r001A25F4` | `0x0043E5F4` | `0x1A25F4` | `0x60` | `trace_func_0025E1B4_r0005EDB4(rdram, ctx);` → `LOOKUP_FUNC(0x0025E1B4)` ×1 |
| `rom_closure_0043EE3C_r001A2E3C` | `0x0043EE3C` | `0x1A2E3C` | `0x84` | `trace_func_0025E1B4_r0005EDB4(rdram, ctx);` → `LOOKUP_FUNC(0x0025E1B4)` ×1 |
| `rom_closure_00445108_r001A9108` | `0x00445108` | `0x1A9108` | `0x88` | `trace_func_0025E1B4_r0005EDB4(rdram, ctx);` → `LOOKUP_FUNC(0x0025E1B4)` ×1 |

Per-function identity and exact patch hashes:

## rom_closure_00204870_r00005470

Guest `0x00204870`, ROM `0x005470`, extent `0x1F8` bytes; original ROM SHA256 `9f1cd82a339653333ea428957253a11bf4b3d3ae69d6d2107003873b07d0f295`.
Accepted closure-specific generated body SHA256 `9146454eb94d43ef8ff6ac53551ba1305503627ae4827211fcda9c41dde98f98`; full-inventory canonical result SHA256 `60b723833077e6ea818014227cefa3e4379086f9f645ae0365f52fe728d1ffe0`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -362,7 +362,7 @@
     // 0x00204A48: jal         0x00214178
     // 0x00204A4C: addiu       $a2, $zero, 0x1
     ctx->r6 = ADD32(0, 0X1);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_2;
     // 0x00204A4C: addiu       $a2, $zero, 0x1
     ctx->r6 = ADD32(0, 0X1);
```

## rom_closure_00204BD0_r000057D0

Guest `0x00204BD0`, ROM `0x0057D0`, extent `0x64` bytes; original ROM SHA256 `0b240aa039a0068f5cf659f0212b0510646371aa0dc0237108ec5ef28fdaae2c`.
Accepted closure-specific generated body SHA256 `b94018d1f382215004d6fd48bb5c4c5aaa118e8f85c1f367628b1c5d63ecba06`; full-inventory canonical result SHA256 `54b802ad9efe0c505663d35587fc94a16ce8fb44abb211e5fde35258eceafad9`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -52,7 +52,7 @@
     // 0x00204C18: jal         0x00214178
     // 0x00204C1C: nop
 
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_1;
     // 0x00204C1C: nop
 
```

## rom_closure_00204DE0_r000059E0

Guest `0x00204DE0`, ROM `0x0059E0`, extent `0x1C` bytes; original ROM SHA256 `38efc74f30f31d80c0cc6ed31ceb1a7eefda3c0ed20e1c738f4f3a8dfe0b7832`.
Accepted closure-specific generated body SHA256 `14a76a0e16a831143673ad0556d41c175ae513eead67381aad7b896b70773dda`; full-inventory canonical result SHA256 `29ad729aea4612fbe52dbfd2165ed1d9a51681f56c552fbceb8ca93af3fa55a0`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -8,7 +8,7 @@
     // 0x00204DE8: jal         0x00214178
     // 0x00204DEC: addu        $a2, $zero, $zero
     ctx->r6 = ADD32(0, 0);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_0;
     // 0x00204DEC: addu        $a2, $zero, $zero
     ctx->r6 = ADD32(0, 0);
```

## rom_closure_00204E2C_r00005A2C

Guest `0x00204E2C`, ROM `0x005A2C`, extent `0x1C` bytes; original ROM SHA256 `9f5e987ecb588d22ab808a020f4dad7d0609c125ab1f020408aad84b166c409b`.
Accepted closure-specific generated body SHA256 `67fabf6246b2b375309e19ef74e6d63ab2847c9a808e7bd5d8f5a51b4e1e2e99`; full-inventory canonical result SHA256 `9d4622dc24a2445833ecd7afe0f0b4a7b147ed3d554b72fd582439dd876deeb2`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -8,7 +8,7 @@
     // 0x00204E34: jal         0x00214178
     // 0x00204E38: addiu       $a2, $zero, 0x1
     ctx->r6 = ADD32(0, 0X1);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_0;
     // 0x00204E38: addiu       $a2, $zero, 0x1
     ctx->r6 = ADD32(0, 0X1);
```

## rom_closure_002050A0_r00005CA0

Guest `0x002050A0`, ROM `0x005CA0`, extent `0x16C` bytes; original ROM SHA256 `33b83bb1d79d824eb3b36ffb9d457305750d2c19d016e0fd0e8c783b0b0a34cf`.
Accepted closure-specific generated body SHA256 `79935729fc7e602e9bbca36e55e3843c40b957ea0b513235e7037034e6dc5550`; full-inventory canonical result SHA256 `a6d4a9f2e290d4e954839418052f1e35d4666e186d85a8f951377822764f7d78`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -266,7 +266,7 @@
     // 0x002051F8: jal         0x00214178
     // 0x002051FC: nop
 
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_0;
     // 0x002051FC: nop
 
```

## rom_closure_0020524C_r00005E4C

Guest `0x0020524C`, ROM `0x005E4C`, extent `0x78` bytes; original ROM SHA256 `18463a6ab5d49e92e5a2b7610296e126c4d2d65a71c446a1ceb624605b758767`.
Accepted closure-specific generated body SHA256 `5a9858a6c53ca3c933bee8d83fcd49ba4e14fc2da1b39a38a4cb2ec96d8eaf26`; full-inventory canonical result SHA256 `fcd06ad9b696a6c7a43a3720c5ce1c904257d8fafe133cf27bf241a8fcb80038`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -40,7 +40,7 @@
     // 0x00205284: jal         0x00214178
     // 0x00205288: addu        $a2, $v1, $zero
     ctx->r6 = ADD32(ctx->r3, 0);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_1;
     // 0x00205288: addu        $a2, $v1, $zero
     ctx->r6 = ADD32(ctx->r3, 0);
@@ -57,7 +57,7 @@
     // 0x00205298: jal         0x00214178
     // 0x0020529C: addu        $a2, $zero, $zero
     ctx->r6 = ADD32(0, 0);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_2;
     // 0x0020529C: addu        $a2, $zero, $zero
     ctx->r6 = ADD32(0, 0);
```

## rom_closure_00205694_r00006294

Guest `0x00205694`, ROM `0x006294`, extent `0x3C` bytes; original ROM SHA256 `033677f3466211a9a9dbeab7aed9e4e6f16c01aa6f654f919f2f112d0f91fc88`.
Accepted closure-specific generated body SHA256 `763b6be2399e38bdb518205121eebc3424f3c3cf818f03d6bbc5c78200e0673f`; full-inventory canonical result SHA256 `922186a95318b1471a89bda9f3b5ba681a03c6f408868c398665bf63cc079c3f`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -12,7 +12,7 @@
     // 0x002056A4: jal         0x00214178
     // 0x002056A8: addiu       $a2, $zero, 0x1
     ctx->r6 = ADD32(0, 0X1);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_0;
     // 0x002056A8: addiu       $a2, $zero, 0x1
     ctx->r6 = ADD32(0, 0X1);
```

## rom_closure_00230AE4_r000316E4

Guest `0x00230AE4`, ROM `0x0316E4`, extent `0xC4` bytes; original ROM SHA256 `40aabcd530e59a597bd7c98e614547fcaec0b85c1ea5b3c2b982798ad73b8e99`.
Accepted closure-specific generated body SHA256 `1b0b4f1f1247113429bb00a50dffc48a3c23f766ea05ae39eb529994bd67848b`; full-inventory canonical result SHA256 `90f4cd1ea83cd03f2d651e5cfcc6f21e2b751b1392fb855c4870d00ae2aa1f90`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -132,7 +132,7 @@
     // 0x00230B8C: jal         0x00214178
     // 0x00230B90: nop
 
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_1;
     // 0x00230B90: nop
 
```

## rom_closure_00231F4C_r00032B4C

Guest `0x00231F4C`, ROM `0x032B4C`, extent `0x25C` bytes; original ROM SHA256 `2ab3a468c015e83fc62290cbea2d9047a4cbf63b8241a273c4d7c8cd02033e67`.
Accepted closure-specific generated body SHA256 `bba9b8cb1b1379011507839553865a2044317d196ae59b76affde2a4f6753257`; full-inventory canonical result SHA256 `e34082b1efbda9f456f90d20650588000c072ca0135a943d843974edb0e10dc0`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -283,7 +283,7 @@
     // 0x002320E0: jal         0x00214178
     // 0x002320E4: addiu       $a2, $zero, 0x5
     ctx->r6 = ADD32(0, 0X5);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_7;
     // 0x002320E4: addiu       $a2, $zero, 0x5
     ctx->r6 = ADD32(0, 0X5);
@@ -394,7 +394,7 @@
     // 0x0023216C: jal         0x00214178
     // 0x00232170: addu        $a2, $s5, $zero
     ctx->r6 = ADD32(ctx->r21, 0);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_12;
     // 0x00232170: addu        $a2, $s5, $zero
     ctx->r6 = ADD32(ctx->r21, 0);
```

## rom_closure_002336FC_r000342FC

Guest `0x002336FC`, ROM `0x0342FC`, extent `0x70` bytes; original ROM SHA256 `931ea35ac147d778f6cc351ccf29c94ce9b2d443e3c30f449db18fb2dfa3c91c`.
Accepted closure-specific generated body SHA256 `6fc844b99bf937f60f2e07b91feff04580dfb5cec8b816ae22da7773893dad06`; full-inventory canonical result SHA256 `3b1a3a9e738b3b97db836410543ed26ee87ffde8c9b8954e0b56b5d96ddd2a4c`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -59,7 +59,7 @@
     // 0x0023374C: jal         0x00214178
     // 0x00233750: addu        $a1, $s0, $zero
     ctx->r5 = ADD32(ctx->r16, 0);
-    resident_wave14_func_00214178(rdram, ctx);
+    LOOKUP_FUNC(0x00214178)(rdram, ctx);
         goto after_1;
     // 0x00233750: addu        $a1, $s0, $zero
     ctx->r5 = ADD32(ctx->r16, 0);
```

## rom_closure_0041CEB0_r00180EB0

Guest `0x0041CEB0`, ROM `0x180EB0`, extent `0x2F4` bytes; original ROM SHA256 `f5f8082e67cd3e51b9a586e0201273c319e2955c6b26e772eb6368f5ec5ad385`.
Accepted closure-specific generated body SHA256 `483beb5089a49bfbdd1f5401dec07bf976cfcbb102a73613ba52e08c675211a5`; full-inventory canonical result SHA256 `268ac5dfa5fbd7c584b03da6a15a93b0e98ba4320fdf234278fa9e316cda1f80`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -120,7 +120,7 @@
     // 0x0041CF60: jal         0x0040ECB0
     // 0x0041CF64: sw          $v0, 0xDC($v1)
     MEM_W(0XDC, ctx->r3) = ctx->r2;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_7;
     // 0x0041CF64: sw          $v0, 0xDC($v1)
     MEM_W(0XDC, ctx->r3) = ctx->r2;
@@ -177,7 +177,7 @@
     // 0x0041CFBC: jal         0x0040ECB0
     // 0x0041CFC0: sh          $v0, 0x14($a2)
     MEM_H(0X14, ctx->r6) = ctx->r2;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_8;
     // 0x0041CFC0: sh          $v0, 0x14($a2)
     MEM_H(0X14, ctx->r6) = ctx->r2;
@@ -234,7 +234,7 @@
     // 0x0041D018: jal         0x0040ECB0
     // 0x0041D01C: sh          $v0, 0x14($a2)
     MEM_H(0X14, ctx->r6) = ctx->r2;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_9;
     // 0x0041D01C: sh          $v0, 0x14($a2)
     MEM_H(0X14, ctx->r6) = ctx->r2;
@@ -264,7 +264,7 @@
     // 0x0041D044: jal         0x0040ECB0
     // 0x0041D048: sw          $v0, 0xE0($v1)
     MEM_W(0XE0, ctx->r3) = ctx->r2;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_11;
     // 0x0041D048: sw          $v0, 0xE0($v1)
     MEM_W(0XE0, ctx->r3) = ctx->r2;
@@ -290,7 +290,7 @@
     // 0x0041D068: jal         0x0040ECB0
     // 0x0041D06C: addiu       $a1, $zero, 0x233
     ctx->r5 = ADD32(0, 0X233);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_13;
     // 0x0041D06C: addiu       $a1, $zero, 0x233
     ctx->r5 = ADD32(0, 0X233);
@@ -316,7 +316,7 @@
     // 0x0041D08C: jal         0x0040ECB0
     // 0x0041D090: addiu       $a1, $zero, 0x1F0
     ctx->r5 = ADD32(0, 0X1F0);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_15;
     // 0x0041D090: addiu       $a1, $zero, 0x1F0
     ctx->r5 = ADD32(0, 0X1F0);
@@ -328,7 +328,7 @@
     // 0x0041D09C: jal         0x0040ECB0
     // 0x0041D0A0: sb          $s0, 0x10($v0)
     MEM_B(0X10, ctx->r2) = ctx->r16;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_16;
     // 0x0041D0A0: sb          $s0, 0x10($v0)
     MEM_B(0X10, ctx->r2) = ctx->r16;
@@ -342,7 +342,7 @@
     // 0x0041D0B0: jal         0x0040ECB0
     // 0x0041D0B4: sb          $v1, 0x10($v0)
     MEM_B(0X10, ctx->r2) = ctx->r3;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_17;
     // 0x0041D0B4: sb          $v1, 0x10($v0)
     MEM_B(0X10, ctx->r2) = ctx->r3;
@@ -378,7 +378,7 @@
     // 0x0041D0E8: jal         0x0040ECB0
     // 0x0041D0EC: sw          $v0, 0xF8($v1)
     MEM_W(0XF8, ctx->r3) = ctx->r2;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_19;
     // 0x0041D0EC: sw          $v0, 0xF8($v1)
     MEM_W(0XF8, ctx->r3) = ctx->r2;
@@ -420,7 +420,7 @@
     // 0x0041D12C: jal         0x0040ECB0
     // 0x0041D130: sw          $v0, 0x104($v1)
     MEM_W(0X104, ctx->r3) = ctx->r2;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_21;
     // 0x0041D130: sw          $v0, 0x104($v1)
     MEM_W(0X104, ctx->r3) = ctx->r2;
@@ -472,7 +472,7 @@
     // 0x0041D174: jal         0x0040ECB0
     // 0x0041D178: addiu       $a1, $zero, 0x1ED
     ctx->r5 = ADD32(0, 0X1ED);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_25;
     // 0x0041D178: addiu       $a1, $zero, 0x1ED
     ctx->r5 = ADD32(0, 0X1ED);
```

## rom_closure_0041D718_r00181718

Guest `0x0041D718`, ROM `0x181718`, extent `0x2B8` bytes; original ROM SHA256 `6bbebe9d13d082c43acb39115ba879bfaaea04f0161e8d230d09443066c6f4bb`.
Accepted closure-specific generated body SHA256 `2e99c887ddd7d5d42cb441a6c18b0e0cbc4c88ba1127592c334499412209baa4`; full-inventory canonical result SHA256 `53ba491458cc06ba5d09b8b33b4f62f21884fda8fcddce04a9058a86d540e6f4`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -20,7 +20,7 @@
     // 0x0041D738: jal         0x0040ECB0
     // 0x0041D73C: addiu       $a1, $zero, 0x1F3
     ctx->r5 = ADD32(0, 0X1F3);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_0;
     // 0x0041D73C: addiu       $a1, $zero, 0x1F3
     ctx->r5 = ADD32(0, 0X1F3);
@@ -66,7 +66,7 @@
     // 0x0041D784: jal         0x0040ECB0
     // 0x0041D788: addiu       $a1, $zero, 0x1F2
     ctx->r5 = ADD32(0, 0X1F2);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_2;
     // 0x0041D788: addiu       $a1, $zero, 0x1F2
     ctx->r5 = ADD32(0, 0X1F2);
@@ -108,7 +108,7 @@
     // 0x0041D7C0: jal         0x0040ECB0
     // 0x0041D7C4: addiu       $a1, $zero, 0x1FD
     ctx->r5 = ADD32(0, 0X1FD);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_5;
     // 0x0041D7C4: addiu       $a1, $zero, 0x1FD
     ctx->r5 = ADD32(0, 0X1FD);
@@ -174,7 +174,7 @@
     // 0x0041D82C: jal         0x0040ECB0
     // 0x0041D830: addiu       $a1, $zero, 0x1F4
     ctx->r5 = ADD32(0, 0X1F4);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_8;
     // 0x0041D830: addiu       $a1, $zero, 0x1F4
     ctx->r5 = ADD32(0, 0X1F4);
@@ -236,7 +236,7 @@
     // 0x0041D890: jal         0x0040ECB0
     // 0x0041D894: addiu       $a1, $zero, 0x1F6
     ctx->r5 = ADD32(0, 0X1F6);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_11;
     // 0x0041D894: addiu       $a1, $zero, 0x1F6
     ctx->r5 = ADD32(0, 0X1F6);
@@ -314,7 +314,7 @@
     // 0x0041D90C: jal         0x0040ECB0
     // 0x0041D910: addu        $s0, $v0, $zero
     ctx->r16 = ADD32(ctx->r2, 0);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_15;
     // 0x0041D910: addu        $s0, $v0, $zero
     ctx->r16 = ADD32(ctx->r2, 0);
@@ -364,7 +364,7 @@
     // 0x0041D958: jal         0x0040ECB0
     // 0x0041D95C: addiu       $a1, $zero, 0x1FA
     ctx->r5 = ADD32(0, 0X1FA);
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_18;
     // 0x0041D95C: addiu       $a1, $zero, 0x1FA
     ctx->r5 = ADD32(0, 0X1FA);
```

## rom_closure_0041DACC_r00181ACC

Guest `0x0041DACC`, ROM `0x181ACC`, extent `0xD4` bytes; original ROM SHA256 `10440cf5649523e04bcd80e6b37fd7de39ebf0adb3489585c9882caffacd089a`.
Accepted closure-specific generated body SHA256 `1c5d9cae906ad49b8dab94e0752dc9d8cdfb92114fa05200b5bf1fcc645c0ad5`; full-inventory canonical result SHA256 `e9baaa6f7f6c653e30ab5d90094cdead13e7aae77236dcff27685a304194b497`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -43,7 +43,7 @@
     // 0x0041DB18: jal         0x0040ECB0
     // 0x0041DB1C: nop
 
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_0;
     // 0x0041DB1C: nop
 
```

## rom_closure_00436488_r0019A488

Guest `0x00436488`, ROM `0x19A488`, extent `0xEC` bytes; original ROM SHA256 `5150d8b4b4f752bf7d4f5862366800f70c2fbcc96d3be5866ca4d748dfeaef24`.
Accepted closure-specific generated body SHA256 `8a18cdaa027a5012b5fbf7c81d2bd99bc7343e3021ac6695d3c938fb6178ab8f`; full-inventory canonical result SHA256 `4da3dec5d8140da4ce3de72955e362026a3afc960d24be8e4c1d70a24ebfd00b`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -114,7 +114,7 @@
     // 0x0043651C: jal         0x00449B20
     // 0x00436520: addu        $a2, $a1, $zero
     ctx->r6 = ADD32(ctx->r5, 0);
-    resident_wave14_external_00449B20(rdram, ctx);
+    LOOKUP_FUNC(0x00449B20)(rdram, ctx);
         goto after_6;
     // 0x00436520: addu        $a2, $a1, $zero
     ctx->r6 = ADD32(ctx->r5, 0);
```

## rom_closure_004378F8_r0019B8F8

Guest `0x004378F8`, ROM `0x19B8F8`, extent `0xA0` bytes; original ROM SHA256 `7d37f89e2723a6e2cc7a55b9218fee6f2ab884d4715e8a2a4019f53ed1e47be2`.
Accepted closure-specific generated body SHA256 `9e0c1d507f0af4703497c177e2bc818500395689995cf6320115d70ec3930f24`; full-inventory canonical result SHA256 `1b46094bab171d8a28e7572c601d6f96b2490c25d7c068da23a0d9c6d640d8b3`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -58,7 +58,7 @@
     // 0x0043794C: jal         0x0040ECB0
     // 0x00437950: sw          $v0, 0x4($v1)
     MEM_W(0X4, ctx->r3) = ctx->r2;
-    trace_func_0040ECB0_r00172CB0(rdram, ctx);
+    LOOKUP_FUNC(0x0040ECB0)(rdram, ctx);
         goto after_3;
     // 0x00437950: sw          $v0, 0x4($v1)
     MEM_W(0X4, ctx->r3) = ctx->r2;
```

## rom_closure_0043E5F4_r001A25F4

Guest `0x0043E5F4`, ROM `0x1A25F4`, extent `0x60` bytes; original ROM SHA256 `200650f5c19555de55fd816da3e910bde7e7579fe5a77eca3c914c597052660d`.
Accepted closure-specific generated body SHA256 `960c1499a294dac51d695264c3871e7b7fd00405fb6709466dd42ea9cb182170`; full-inventory canonical result SHA256 `ae90404978103679c41926fc001a7ab79f95892f5354f54b6b1965fbe7c25ba4`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -24,7 +24,7 @@
     // 0x0043E614: jal         0x0025E1B4
     // 0x0043E618: addiu       $a0, $zero, -0x1
     ctx->r4 = ADD32(0, -0X1);
-    trace_func_0025E1B4_r0005EDB4(rdram, ctx);
+    LOOKUP_FUNC(0x0025E1B4)(rdram, ctx);
         goto after_1;
     // 0x0043E618: addiu       $a0, $zero, -0x1
     ctx->r4 = ADD32(0, -0X1);
```

## rom_closure_0043EE3C_r001A2E3C

Guest `0x0043EE3C`, ROM `0x1A2E3C`, extent `0x84` bytes; original ROM SHA256 `1b67d53d78be285a66c0f5535728810cab7bb40cb88225540fc590610ba4935e`.
Accepted closure-specific generated body SHA256 `5463c8cb18a1ae51d00a560042e108f817ca6b77567d80f6cc3ce7c68b26b718`; full-inventory canonical result SHA256 `811fa8e2f06721d0cbf01cf79ba16ddb2dd2d56646d92efc144351145ce01cd1`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -16,7 +16,7 @@
     // 0x0043EE54: jal         0x0025E1B4
     // 0x0043EE58: addiu       $a0, $zero, -0x1
     ctx->r4 = ADD32(0, -0X1);
-    trace_func_0025E1B4_r0005EDB4(rdram, ctx);
+    LOOKUP_FUNC(0x0025E1B4)(rdram, ctx);
         goto after_0;
     // 0x0043EE58: addiu       $a0, $zero, -0x1
     ctx->r4 = ADD32(0, -0X1);
```

## rom_closure_00445108_r001A9108

Guest `0x00445108`, ROM `0x1A9108`, extent `0x88` bytes; original ROM SHA256 `5f3b259ea136e1594031395dc544feed2f213dd6416279008b29ddd27a1994e1`.
Accepted closure-specific generated body SHA256 `26f0f83bd0c551584070be694f51216a148907ac1c42cb723c7f27b1fcc06b89`; full-inventory canonical result SHA256 `8096e3ad1a1344db4b6be09c079c0c43e4177b27a4ba253ae1f43447eb32ccff`. The accepted source body matches the closure-specific result.

```diff
--- reviewed-closure
+++ full-canonical
@@ -20,7 +20,7 @@
     // 0x00445128: jal         0x0025E1B4
     // 0x0044512C: addiu       $a0, $zero, -0x1
     ctx->r4 = ADD32(0, -0X1);
-    trace_func_0025E1B4_r0005EDB4(rdram, ctx);
+    LOOKUP_FUNC(0x0025E1B4)(rdram, ctx);
         goto after_0;
     // 0x0044512C: addiu       $a0, $zero, -0x1
     ctx->r4 = ADD32(0, -0X1);
```

