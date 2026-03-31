# Shenandoah ARM32 Test Session - Critical State

## Test Results Summary (as of this session)
- **22 PASS**: All adaptive, static, compact, passive mode tests work perfectly
- **11 FAIL**: ALL aggressive mode tests crash (SIGSEGV=134 / OOM-killed=137)
- **TestVerifyJCStress adaptive**: TIMEOUT at 180s (needs longer, not a crash)

## Root Cause Analysis - Aggressive Mode Crashes

### Crash Pattern
- Crashes happen VERY EARLY (~0.6s) during System.initPhase2() / ModuleBootstrap.boot2()
- SIGSEGV in C1-compiled ModuleReference.descriptor() - simple getter method
- `this` pointer resolves to from-space object that was already recycled
- Field load [this + 8] returns garbage (0xfe959381) since memory was reused
- LRB tries to check if garbage value is in cset -> crashes at cset_table[garbage >> 18]

### Why Aggressive Mode Is Different
From shenandoahAggressiveHeuristics.cpp:
- should_start_gc() ALWAYS returns true -> GC cycles back-to-back
- choose_collection_set ALL regions with ANY garbage in CSet
- ShenandoahImmediateThreshold = 100 -> no evacuation shortcuts

### Where The Stale Reference Comes From
- Interpreter prepare_invoke (templateTable_arm.cpp:3590-3610) has inline forwarding resolution
- This ONLY resolves forwarding pointers - cannot handle recycled from-space
- Stack watermark processing SHOULD update all stack oops before from-space recycling
- KEY QUESTION: Is stack watermark processing working correctly on ARM32?

## Key Source Files
- shenandoahBarrierSetAssembler_arm.cpp - All ARM32 barriers
- shenandoahAggressiveHeuristics.cpp - Aggressive mode behavior
- templateTable_arm.cpp:3550-3620 - prepare_invoke with Shenandoah forwarding
- frame_arm.cpp:472 - StackWatermarkSet::on_iteration
- shenandoahStackWatermark.cpp - Stack watermark processing

## Build / Deploy / Test Commands

### Build JDK
```bash
wsl bash -c "cd /mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah && bash build-fast.sh 2>&1"
```

### Deploy to RIO
```bash
wsl -e bash -c "scp -o StrictHostKeyChecking=no /mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/"
wsl -e bash -c "ssh -o StrictHostKeyChecking=no admin@10.59.40.2 'cd / && opkg install frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite'"
```

### Compile Tests (in Docker)
```bash
wsl -e bash -c "docker exec shenandoah-builder bash -c 'cd /artifacts/jdk_tests/shenandoah_suite && javac -source 11 -target 11 *.java'"
```

### Deploy Tests
```bash
wsl -e bash -c "scp -o StrictHostKeyChecking=no /mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah/jdk_tests/shenandoah_suite/*.class /mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah/jdk_tests/run_shenandoah_tests.sh lvuser@10.59.40.2:/home/lvuser/shenandoah_tests/"
```

### Run Tests
```bash
wsl -e bash -c "ssh -o StrictHostKeyChecking=no lvuser@10.59.40.2 'bash /home/lvuser/shenandoah_tests/run_shenandoah_tests.sh 2>&1'"
```

## AllocTest2 Memory Analysis
500MB target with 64MB heap is NOT OOM - objects immediately discarded.
User says 150m heap is fine for rio (497MB RAM).
The NPE at System.out was a Shenandoah GC bug, not memory exhaustion.

## Detailed Investigation (Session 2)

### Stack Watermark Processing
- `ShenandoahStackWatermark::process()` calls `fr.oops_do(closure, cb_cl, register_map)`
- During evacuation: uses `ShenandoahEvacuateUpdateMetadataClosure` (evacs + updates oop slots in-place)
- `closure_from_context(NULL)` only handles: mark-in-progress OR weak-root-in-progress
- Does NOT handle update-refs phase → hits ShouldNotReachHere() if called
- BUT: watermark processing is already complete by update-refs phase (no-op)

### op_update_thread_roots() Handshake Flow
1. `jt->oops_do(&ShenandoahUpdateRefsClosure, NULL /*no CodeBlobClosure*/)`
2. Calls `oops_do_frames()` → `StackWatermarkSet::finish_processing()` (no-op, already done)
3. Walks ALL frames with `ShenandoahUpdateRefsClosure`: for each oop slot, if forwarded → update
4. This SHOULD cover all interpreter frame locals, expression stack, monitors

### Region Lifecycle
- `make_trash()` → just sets state to _trash (data intact)
- `recycle()` → sets top=bottom, makes empty_committed (data still there unless ZapUnusedHeapArea)
- `recycle_trash()` → concurrent, called at cleanup_complete phase
- After recycle, region available for new allocations → old data can be overwritten

### Crash Instruction Decode (hs_err_pid2041.log)
```
ldr r1, [r0, #8]        ; load this.descriptor from ModuleReference
add r2, r0, #8           ; addr of field (for barrier)
mov r0, r1               ; result = loaded descriptor
ldrsb r3, [r10, #0x10]   ; gc_state from thread
mov r4, #1               ; HAS_FORWARDED mask
and r3, r3, r4           ; check has_forwarded
cmp r3, #0
bne slow_path            ; if forwarded, go to barrier
... fast return ...
; SLOW PATH:
mov r0, r1               ; r0 = loaded descriptor oop (GARBAGE: 0xfe959381)
mov r4, #0x8000           ; cset table base
lsr r3, r0, #18           ; region index
ldrb r4, [r4, r3]         ; cset_table[idx] → CRASH at 0xbfa5 (unmapped)
```

### Aggressive Mode (shenandoahAggressiveHeuristics.cpp)
- should_start_gc() → always true (back-to-back GC cycles)
- choose_collection_set → ALL regions with any garbage
- ShenandoahImmediateThreshold = 100 (no shortcuts)
- This is a DIAGNOSTIC mode, not meant for production

### Key Decision Point
Aggressive mode is a diagnostic/testing mode. ALL production modes (adaptive, static, compact, passive) pass.
For FRC usage, aggressive mode is irrelevant. The question is:
- Should we fix aggressive mode crashes? (complex investigation, unclear ROI)
- Or proceed with robot code stability test? (the actual goal)

## What's Next
1. Decide: fix aggressive mode or skip it (user decision)
2. Consider just adding -XX:ShenandoahGCHeuristics=aggressive to known-unsupported
3. Run robot code stability test (the real goal)
4. Or investigate deeper: check oops_interpreted_do() ARM32 for missing oop slots

## Build / Deploy / Test Commands (verified working)

### Build JDK
```bash
wsl bash -c "cd /mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah && bash build-fast.sh 2>&1"
```

### Deploy to RIO
```bash
wsl -e bash -c "scp -o StrictHostKeyChecking=no /mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah/frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk admin@10.59.40.2:/"
wsl -e bash -c "ssh -o StrictHostKeyChecking=no admin@10.59.40.2 'cd / && opkg install frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite'"
```

### Compile Tests (in Docker)
```bash
wsl -e bash -c "docker exec shenandoah-builder bash -c 'cd /artifacts/jdk_tests/shenandoah_suite && javac -source 11 -target 11 *.java'"
```

### Deploy Tests
```bash
wsl -e bash -c "scp -o StrictHostKeyChecking=no /mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah/jdk_tests/shenandoah_suite/*.class /mnt/c/Users/vasis/Desktop/frc-openjdk-roborio-shenandoah/jdk_tests/run_shenandoah_tests.sh lvuser@10.59.40.2:/home/lvuser/shenandoah_tests/"
```

### Run Tests
```bash
wsl -e bash -c "ssh -o StrictHostKeyChecking=no lvuser@10.59.40.2 'bash /home/lvuser/shenandoah_tests/run_shenandoah_tests.sh 2>&1'"
```
