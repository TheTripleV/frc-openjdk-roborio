#!/bin/bash
# Add -Xint to AGG-11 (line 121) in run_all_rio_tests.sh
sed -i '121s/-Xmx150m/-Xint -Xmx150m/' /home/lvuser/shenandoah_tests/run_all_rio_tests.sh
echo "Fixed AGG-11 line 121:"
sed -n '121p' /home/lvuser/shenandoah_tests/run_all_rio_tests.sh
