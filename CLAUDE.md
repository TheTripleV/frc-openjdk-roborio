see shenandoah_plan.md

We want to make shenandoah gc work on the roborio.
This means the full version of shenandoah on arm32sf as it is currently supported on arm64. The goal is to get really low pause times.


Loop instructions:

1. Make code fixes
2. Make sure the build passes (use build-fast.sh for incremental builds ~3min, vs ~20min for full rebuilds)
`bash build-fast.sh 2>&1; echo "Exit: $? Elapsed: ${SECONDS}s"`
To reset the persistent container: `docker rm -f shenandoah-builder`

3. Put the ipk on the rio. It's at the root / folder. the rio is at 10.59.40.2. The username is admin and there is no password.
4. Install the ipk on the rio as admin.
`cd /`
`opkg install frc2024-openjdk-17-jre_17.0.9u7-3_cortexa9-vfpv3.ipk --force-reinstall --force-overwrite`

5. Run test scripts on the rio to make sure the gc is working and that java is not crashing.

6. Start the robot code and make sure it doesn't crash on the rio
login with username "lvuser", no password
`cd ~`
`./robotCommand`

6. Make sure it doesn't crash. You can add extra debug info to the java build and the gc find where the bugs are.

-----

If the instructions need to be clarified, add to this file as needed.

-----