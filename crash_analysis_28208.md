# hs_err_pid28208 Crash Analysis - COMPLETE DATA
# File: /home/lvuser/hs_err_pid28208.log on RoboRIO 10.59.40.2
# Total lines: 2114
# Date: Tue Apr 7 21:19:34 2026 UTC

---

## 1. CRASH HEADER
```
#
# A fatal error has been detected by the Java Runtime Environment:
#
#  SIGSEGV (0xb) at pc=0xb40d9b80, pid=28208, tid=28209
#
# JRE version: OpenJDK Runtime Environment (17.0.9.7) (build 17.0.9.7-frc+0-2024-17.0.9u7-3)
# Java VM: OpenJDK Client VM (17.0.9.7-frc+0-2024-17.0.9u7-3, mixed mode, emulated-client, shenandoah gc, linux-arm)
# Problematic frame:
# j  java.lang.Character.getType(I)I+5 java.base@17.0.9.7-frc
#

siginfo: si_signo: 11 (SIGSEGV), si_code: 1 (SEGV_MAPERR), si_addr: 0x400f6000

Host: rev 0 (v7l), 2 cores, 497M, NI Linux Real-Time - Academic 8.15
Time: Tue Apr  7 21:19:34 2026 UTC elapsed time: 63.817549 seconds (0d 0h 1m 3s)

Current thread (0xb638d418):  JavaThread "main" [_thread_in_Java, id=28209, stack(0xb648d000,0xb64dd000)]
VM state: not at safepoint (normal execution)

Command Line: -Djava.lang.invoke.stringConcat=BC_SB -Djava.library.path=/usr/local/frc/third-party/lib 
              -XX:+UnlockDiagnosticVMOptions -XX:+UnlockExperimentalVMOptions -XX:+UseShenandoahGC -Xlog:gc 
              /home/lvuser/2026-Delta.jar
```

---

## 2. REGISTERS
```
  r0  = 0x00002030
  r1  = 0x400f6000
  r2  = 0x400f6000
  r3  = 0x40000002
  r4  = 0x00000001
  r5  = 0xaabc4a80
  r6  = 0xb6d804b8
  r7  = 0xaa0b15a5
  r8  = 0xb64da4cc
  r9  = 0x0000000e
  r10 = 0xb638d418
  fp  = 0xb64da4c4
  r12 = 0x00000002
  sp  = 0xb64da498
  lr  = 0xb40cfc38
  pc  = 0xb40d9b80
  cpsr = 0x20000010
```

---

## 3. REGISTER TO MEMORY MAPPING
```
  r0  = 0x00002030 is an unknown value
  r1  = 0x400f6000 is an unknown value
  r2  = 0x400f6000 is an unknown value
  r3  = 0x40000002 is an unknown value
  r4  = 0x00000001 is an unknown value
  r5  = 0xaabc4a80 is a pointer to class: java.util.function.IntPredicate {0xaabc4a80}
         - instance size: 2
         - klass size: 74
  r6  = 0xb6d804b8: <offset 0x0061b4b8> in /usr/local/frc/JRE/lib/client/libjvm.so at 0xb6765000
  r7  = 0xaa0b15a5 is pointing into metadata
  r8  = 0xb64da4cc is pointing into the stack for thread: 0xb638d418
  r9  = 0x0000000e is an unknown value
  r10 = 0xb638d418 is a thread
  fp  = 0xb64da4c4 is pointing into the stack for thread: 0xb638d418
  r12 = 0x00000002 is an unknown value
  sp  = 0xb64da498 is pointing into the stack for thread: 0xb638d418
  lr  = 0xb40cfc38 is at code_begin+1016 in an Interpreter codelet: return entry points [0xb40cf840, 0xb40d0230] 2544 bytes
  pc  = 0xb40d9b80 is at code_begin+208 in an Interpreter codelet: invokevirtual 182 invokevirtual [0xb40d9ab0, 0xb40d9be0] 304 bytes
```

---

## 4. TOP OF STACK (sp=0xb64da498)
```
0xb64da498:   00002030 400f6000 b64da4a0 aa0b15a5
0xb64da4a8:   b64da4cc aa13dce8 00000000 ac184658
0xb64da4b8:   aa0b15b0 00000000 b64da4cc b64da4f4
0xb64da4c8:   b40cfc8c 00002030 b64da4d0 aabbbc19
0xb64da4d8:   b64da4fc aabbbe70 00000000 ac223090
0xb64da4e8:   aabbbc28 b64da4cc b64da4fc b64da524
0xb64da4f8:   b40cfc8c 00002030 b64da500 aabc4d11
0xb64da508:   b64da530 aabc4f70 00000000 ac2289a0
0xb64da518:   aabc4d18 b64da4fc b64da52c b64da558
0xb64da528:   b40d0084 00002030 ac228bb8 b64da534

Stack slot to memory mapping:
  sp + 0 slots: 0x00002030 is an unknown value
  sp + 1 slots: 0x400f6000 is an unknown value                  *** GARBAGE OOP ON STACK ***
  sp + 2 slots: 0xb64da4a0 is pointing into the stack for thread: 0xb638d418
  sp + 3 slots: 0xaa0b15a5 is pointing into metadata
  sp + 4 slots: 0xb64da4cc is pointing into the stack for thread: 0xb638d418
  sp + 5 slots: 0xaa13dce8 is pointing into metadata
  sp + 6 slots: 0x0 is NULL
  sp + 7 slots: 0xac184658 is an oop: java.lang.Class
    {0xac184658} - klass: 'java/lang/Class'
     - private transient 'name' 'Ljava/lang/String;' @32  "java.lang.Character"{0xb3da8a38}
     - signature: Ljava/lang/Character;
     - fake entry for mirror: 'java/lang/Character'
     - fake entry for array: 'java/lang/Character'[]
     - fake entry for oop_size: 54
     - fake entry for static_oop_field_count: 1

[error occurred during error reporting (inspecting top of stack), id 0xb, SIGSEGV (0xb) at pc=0xb6909598]
```

NOTE: Error reporter itself crashed while trying to inspect 0x400f6000 on the stack!

---

## 5. INSTRUCTIONS (pc=0xb40d9b80, crash point)

### Full invokevirtual codelet disassembly [0xb40d9ab0, 0xb40d9be0] 304 bytes:
```
[MachCode]
  0xb40d9ab0: 010a 2ded | 0400 00ea | 020b 2ded | 0200 00ea | 0300 2de9 | 0000 00ea | 0400 2de5 | 2070 0be5
  0xb40d9ad0: 0230 d7e5 | 0120 d7e5 | 0334 82e1 | 1820 1be5 | 0331 a0e1 | 03c1 82e0 | 13c0 dce5 | 5ff0 7ff5
  0xb40d9af0: b600 5ce3 | 1900 000a | b610 a0e3 | 2070 0be5 | f0b1 8ae5 | ecf1 8ae5 | e8d1 8ae5 | 07d0 cde3
  0xb40d9b10: 0a00 a0e1 | b1e3 07e3 | 9de6 4be3 | 3eff 2fe1 | e8d1 9ae5 | 00c0 a0e3 | e8c1 8ae5 | f0c1 8ae5
  0xb40d9b30: ecc1 8ae5 | 04c0 9ae5 | 0000 5ce3 | 0f50 a011 | a2cd ff1a | 2070 1be5 | 0c90 1be5 | 0230 d7e5
  0xb40d9b50: 0120 d7e5 | 0334 82e1 | 1820 1be5 | 0331 a0e1 | 03c1 82e0 | 1890 9ce5 | 1c30 9ce5 | ffc0 03e2
  0xb40d9b70: 0c21 8de0 | 0420 12e5 | 0000 52e3 | 0500 000a | 00c0 92e5 | 0cc0 e0e1 | 0300 1ce3 | 0100 001a
  0xb40d9b90: 03c0 8ce3 | 0c20 e0e1 | 231e a0e1 | e0c9 08e3 | d7c6 4be3 | 01e1 9ce7 | 4009 13e3 | 0300 000a
  0xb40d9bb0: 00c0 92e5 | 0d40 a0e1 | 0840 0be5 | 30f0 99e5 | 0420 92e5 | 09c1 82e0 | 1091 9ce5 | 0d40 a0e1
  0xb40d9bd0: 0840 0be5 | 30f0 99e5 | 0000 0000 | 0000 0000
[/MachCode]
```

### Annotated disassembly of crash area:
```asm
; --- Compute receiver object address from interpreter stack ---
0xb40d9b60: add r12, r2, r3, lsl #2      ; compute vtable entry offset
0xb40d9b64: ldr r9, [r12, #0x18]          ; load vtable method entry
0xb40d9b68: ldr r3, [r12, #0x1c]          ; load method descriptor
0xb40d9b6c: and r12, r3, #255             ; mask low byte (stack arg count)
0xb40d9b70: add r2, sp, r12, lsl #2       ; compute stack slot address
0xb40d9b74: ldr r2, [r2, #-4]             ; *** LOAD RECEIVER OOP FROM STACK ***
                                           ;     r2 = return value from CharacterData.of(int)
                                           ;     r2 = 0x400f6000 (GARBAGE!)
0xb40d9b78: cmp r2, #0                    ; null check
0xb40d9b7c: beq +5                        ; if null, skip LRB

; --- SHENANDOAH LOAD REFERENCE BARRIER (LRB) INLINE ---
0xb40d9b80: ldr r12, [r2]                 ; *** CRASH! Load forwarding ptr from oop ***
                                           ;     r2=0x400f6000: UNMAPPED -> SIGSEGV
0xb40d9b84: mvn r12, r12                  ; bitwise NOT of forwarding ptr
0xb40d9b88: tst r12, #3                   ; test low 2 bits (forwarded?)
0xb40d9b8c: bne slow_path                 ; if forwarded, go to slow path
0xb40d9b90: orr r12, r12, #3             ; set forwarding bits
0xb40d9b94: mvn r2, r12                   ; r2 = resolved (to-space) oop

; --- AFTER LRB: dispatch to method ---
0xb40d9b98: lsr r1, r3, #28              ; extract method type flags
0xb40d9b9c: movw r12, #0x89e0            ; dispatch table base (low)
0xb40d9ba0: movt r12, #0xb6d7            ; dispatch table base (high) -> 0xb6d789e0
0xb40d9ba4: ldr lr, [r12, r1, lsl #2]    ; load handler from dispatch table
0xb40d9ba8: tst r3, #0x940               ; test more method flags
0xb40d9bac: beq fast_invoke              ; if no special flags, jump

; --- SLOW PATH (re-resolve + dispatch) ---
0xb40d9bb0: ldr r12, [r2]                ; re-read forwarding ptr
0xb40d9bb4: mov r4, sp                   ; save sp
0xb40d9bb8: str r4, [fp, #-8]           ; store last_sp
0xb40d9bbc: ldr pc, [r9, #0x30]         ; jump to compiled method entry

; --- FAST PATH ---
0xb40d9bc0: ldr r12, [r2, #4]           ; load klass from oop
0xb40d9bc4: add r12, r2, r9, lsl #2     ; offset into object
0xb40d9bc8: ldr r9, [r12, #0x10]        ; load method
0xb40d9bcc: mov r4, sp                   ; save sp
0xb40d9bd0: str r4, [fp, #-8]           ; store last_sp
0xb40d9bd4: ldr pc, [r9, #0x30]         ; dispatch
```

---

## 6. JAVA STACK TRACE (all frames interpreted - 'j' prefix)
```
j  java.lang.Character.getType(I)I+5 java.base@17.0.9.7-frc                                    <-- CRASH
j  java.text.DecimalFormatSymbols.lambda$findNonFormatChar$0(I)Z+1
j  java.text.DecimalFormatSymbols$$Lambda$178+0xaabc4d50.test(I)Z+1
j  java.util.stream.IntPipeline$10$1.accept(I)V+8
j  java.lang.StringUTF16$CharsSpliterator.tryAdvance(Ljava/util/function/IntConsumer;)Z+38
j  java.util.stream.IntPipeline.forEachWithCancel(Ljava/util/Spliterator;Ljava/util/stream/Sink;)Z+26
j  java.util.stream.AbstractPipeline.copyIntoWithCancel(Ljava/util/stream/Sink;Ljava/util/Spliterator;)Z+32
j  java.util.stream.AbstractPipeline.copyInto(Ljava/util/stream/Sink;Ljava/util/Spliterator;)V+49
j  java.util.stream.AbstractPipeline.wrapAndCopyInto(Ljava/util/stream/Sink;Ljava/util/Spliterator;)Ljava/util/stream/Sink;+13
j  java.util.stream.FindOps$FindOp.evaluateSequential(Ljava/util/stream/PipelineHelper;Ljava/util/Spliterator;)Ljava/lang/Object;+14
j  java.util.stream.AbstractPipeline.evaluate(Ljava/util/stream/TerminalOp;)Ljava/lang/Object;+88
j  java.util.stream.IntPipeline.findFirst()Ljava/util/OptionalInt;+5
j  java.text.DecimalFormatSymbols.findNonFormatChar(Ljava/lang/String;C)C+14
j  java.text.DecimalFormatSymbols.initialize(Ljava/util/Locale;)V+206
j  java.text.DecimalFormatSymbols.<init>(Ljava/util/Locale;)V+11
j  sun.util.locale.provider.DecimalFormatSymbolsProviderImpl.getInstance(...)Ljava/text/DecimalFormatSymbols;+17
j  java.text.DecimalFormatSymbols.getInstance(...)Ljava/text/DecimalFormatSymbols;+14
j  sun.util.locale.provider.NumberFormatProviderImpl.getInstance(...)Ljava/text/NumberFormat;+51
j  sun.util.locale.provider.NumberFormatProviderImpl.getIntegerInstance(...)Ljava/text/NumberFormat;+3
j  java.text.NumberFormat.getInstance(...)Ljava/text/NumberFormat;+74
j  java.text.NumberFormat.getInstance(...)Ljava/text/NumberFormat;+11
j  java.text.NumberFormat.getIntegerInstance(...)Ljava/text/NumberFormat;+3
j  java.text.SimpleDateFormat.initialize(Ljava/util/Locale;)V+37
j  java.text.SimpleDateFormat.<init>(...)V+66
j  com.fasterxml.jackson.databind.util.StdDateFormat.<clinit>()V+83                             <-- CLINIT TRIGGER
v  ~StubRoutines::call_stub
```

---

## 7. SHENANDOAH HEAP STATE AT CRASH
```
GC Precious Log:
 CPUs: 2 total, 2 available
 Memory: 497M
 Large Page Support: Disabled
 NUMA Support: Disabled
 Compressed Oops: Disabled
 Heap Min Capacity: 5M
 Heap Initial Capacity: 8M
 Heap Max Capacity: 127488K
 Pre-touch: Disabled
 Parallel Workers: 1
 Concurrent Workers: 1

Shenandoah Heap
 124M max, 124M soft max, 38912K committed, 9122K used
 498 x 256K regions
Status: not cancelled
Reserved region:
 - [0xac180000, 0xb3e00000) 
Collection set:
 - map (vanilla): 0x0000ab06
 - map (biased):  0x00008000

ShenandoahBarrierSet
Polling page: 0xb6f4b000

Selected regions (showing only non-empty/interesting):
  Region 0  |R  | ac180000-ac1c0000 | L=243K                   (regular, live)
  Region 1  |R  | ac1c0000-ac200000 | L=0                       (regular, no live data) 
  Region 2  |R  | ac200000-ac240000 | L=0                       (regular, no live data)
  Region 3  |R  | ac240000-ac280000 | S=90600B                  (regular, shared alloc)
  Region 4  |R  | ac280000-ac2c0000 | L=0                       (regular)
  Region 5-6|EC |                    |                           (empty committed)
  Region 7  |H  | ac340000-ac380000 | S=256K, L=256K            (humongous start)
  Region 8  |HC | ac380000-ac3bb7f8 | S=237K, L=237K            (humongous continuation)
  Regions 9-124 |EC| empty committed
  Regions 125-141 |R| regular, L=256K (live)                     (recently evacuated to)
  Regions 142-497 |EU| empty uncommitted
```

---

## 8. GC TIMELINE (critical for understanding crash)
```
VM OPERATIONS:
  62.097  Shenandoah Init Marking
  62.098  Shenandoah Init Marking done
  62.589  Shenandoah Final Mark and Start Evacuation
  62.590  Shenandoah Final Mark and Start Evacuation done

CONCURRENT EVENTS:
  62.943  Concurrent strong roots
  62.947  Concurrent strong roots done
  62.947  Concurrent evacuation
  63.255  Concurrent evacuation done                        (+308ms evacuation)
  63.273  Pause Init Update Refs
  63.273  Pause Init Update Refs done
  63.274  Concurrent update references
  63.418  Concurrent update references done                 (+144ms update refs)
  63.418  Concurrent update thread roots
  63.425  Concurrent update thread roots done
  63.426  Pause Final Update Refs
  63.426  Pause Final Update Refs done
  63.426  Concurrent cleanup
  63.426  Concurrent cleanup done                           *** GC CYCLE COMPLETE ***

CLASS LOADING AFTER GC:
  63.577  loading class LocaleResources
  63.598  loading class Gregorian$Date
  63.601  loading class DateFormatSymbols
  63.654  loading class NumberFormat
  63.673  loading class DecimalFormatSymbols
  63.802  loading class java/lang/CharacterData00            (+376ms after GC complete)
  63.803  loading class java/lang/CharacterData00 done

CRASH:
  63.817  SIGSEGV                                           (+391ms after GC, +14ms after CharacterData00 load)
```

**GC STATE AT CRASH: gc_state=0 (idle). The GC cycle completed 391ms before the crash.**

---

## 9. COMPILATION EVENTS

Last 250 compilation events shown (IDs 507-636, from 61.0s-63.8s).
**CharacterData.of is NOT listed** - it was compiled early (likely ID ~50 at t~0.7s as seen in 
previous crash pid 28095) and rotated out of the 250-event buffer.

**Character.getType(I)I is NOT compiled** - all frames are interpreted ('j' prefix).

No deoptimization events recorded. No classes unloaded.

Notable late compilations near crash time:
```
Event: 63.751 Thread 0xab6416e0  635  !  java.lang.invoke.MemberName::clone (14 bytes)
Event: 63.791 Thread 0xab6416e0  636     jdk.internal.org.objectweb.asm.AnnotationWriter::putAnnotations (67 bytes)
```

---

## 10. INTERNAL EXCEPTIONS
All 24 internal exceptions are normal `java.lang.NoSuchMethodError` for method handle generation
(DirectMethodHandle$Holder.invokeSpecial/invokeStatic/etc). These are expected during lambda/
method-handle spin-up and are NOT related to the crash.

---

## 11. MEMORY MAP ANALYSIS - 0x400f6000

**0x400f6000 is COMPLETELY UNMAPPED.** No mapping exists anywhere near it.

The memory map shows:
```
Native heap:  01f75000-01fdf000  (small)
First library: a3c00000-a3c57000  (libapriltag.so)
...libraries  continues through a3... a4... a5... a9... aa... ab...
Java Heap:    ac180000-b3e00000   (Shenandoah reserved region)
Code Cache:   b40cd000-b4205000   (estimated)
libjvm.so:    b6765000-b6d89000
```

There is a gap from 0x01fdf000 to 0xa3c00000 — approximately a 2.5GB unmapped region.
0x400f6000 falls squarely in this unmapped gap.

The GC thread stack is at `0xb3f8d000-0xb400d000` — note 0x400f6000 is NOT within any 
thread stack either (the nearest end is 0xb400d000, 0x740f3000 away).

---

## 12. CRITICAL OBSERVATIONS

1. **r3 = 0x40000002**: Note that r3 has a suspiciously similar prefix (0x40000000 + 2).
   This could be a corrupted value that's been slightly modified. r2 = 0x400f6000 could be
   derived from r3 or share a common corruption source.

2. **The LRB is in the INTERPRETER, not in compiled code**: The crash is at pc=0xb40d9b80 which
   is inside the `invokevirtual` interpreter codelet. This means the interpreter itself is 
   trying to apply the Shenandoah LRB on the **return value** of a method call.

3. **The garbage value 0x400f6000 came from CharacterData.of(int)**: At 0xb40d9b74 (`ldr r2, [r2, #-4]`),
   r2 was loaded from the interpreter expression stack. This stack slot contains the return 
   value from the previous method call (CharacterData.of(int)), which is C1-compiled.

4. **GC cycle completed 391ms before crash**: This rules out concurrent GC state issues at 
   crash time. The stale oop was "baked in" to the C1 compiled code during or before the GC 
   cycle and survived into the post-GC idle period.

5. **Error reporter itself crashed**: While trying to inspect the stack slot at sp+1 
   (0x400f6000), the error reporter also crashed with SIGSEGV, confirming the address is 
   totally invalid.

6. **No deoptimization events**: The C1-compiled CharacterData.of was never deoptimized,
   meaning its embedded oop constant was never invalidated/recompiled.

---

## 13. ROOT CAUSE HYPOTHESIS (confirmed by prior investigation)

The C1-compiled `CharacterData.of(int)` method has an embedded oop constant (mirror of 
CharacterData00) loaded via `movw/movt` instructions. After Shenandoah GC relocates the mirror,
the `fix_oop_relocations` path should patch the movw/movt instructions in the nmethod. However,
on ARM32, `oop_is_immediate()` returns false (because mov_oop uses oop_index > 0 via 
`allocate_oop_index()`), causing `fix_oop_relocation()` in relocInfo.cpp to skip patching the
embedded instructions. The oop table IS updated, but the movw/movt instructions still contain
the OLD (from-space) address. When that from-space region is recycled, the old address becomes
garbage (0x400f6000 or similar), and the C1-compiled method returns this garbage to the 
interpreter, which then crashes in the LRB.
