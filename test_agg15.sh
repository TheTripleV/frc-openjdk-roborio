#!/bin/bash
cd /home/lvuser/shenandoah_tests
/usr/local/frc/JRE/bin/java \
  -XX:+UnlockDiagnosticVMOptions \
  -XX:+UnlockExperimentalVMOptions \
  -XX:+UseShenandoahGC \
  -Xmx150m -Xms150m \
  -XX:ShenandoahGCHeuristics=aggressive \
  -DouterCount=200 -DinnerCount=2000 \
  -cp . TestVerifyJCStress 2>&1
echo "EXITCODE=$?"
