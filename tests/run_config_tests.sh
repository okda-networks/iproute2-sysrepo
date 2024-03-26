#!/bin/bash

# Set default return value
ret=0

# Start iproute2-sysrepo
./bin/iproute2-sysrepo 2>&1 &
sysrepo_pid=$!
sleep 0.5

#Save current running_ds state to startup_ds
sysrepocfg -C running -d startup

# Iterate over all case/*.sh files
for test_script in tests/cases/*.sh; do
    # Check if the file is executable and has the .sh extension
    if [ -x "$test_script" ] && [[ "$test_script" == *.sh ]]; then
        echo "#############################################"
        echo "#####  Running case $test_script"
        echo "#############################################"
        ./"$test_script"
        script_ret=$?  # Capture the return value of the test script
        if [ $script_ret -ne 0 ]; then
            echo "Error: $test_script failed with exit code $script_ret"
            kill $sysrepo_pid
            wait $sysrepo_pid
            exit $script_ret
        fi
        echo "Completed $test_script"
    fi
done

## cleanup
kill $sysrepo_pid
wait $sysrepo_pid

# Check if the previous steps encountered any errors
if [ $ret -ne 0 ]; then
    echo "TEST-ERROR: One or more test scripts failed. (FAIL)"
    exit $ret
else
   echo "TEST-INFO: All Tests Completed Successfully (PASS)"
fi

# Exit with return value
exit $ret
