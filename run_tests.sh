#!/bin/bash

# Set default return value
ret=0

# Step 1: Install YANG Modules
for yang_file in yang/*.yang; do
    echo "Installing $yang_file..."
    # Install the YANG module
    sysrepoctl -i "$yang_file" -s ./yang || ret=$?
    if [ $ret -ne 0 ]; then
        echo "Error: Failed to install $yang_file"
        exit $ret
    fi
done


# Step 2: Start iproute2-sysrepo
./bin/iproute2-sysrepo 2>&1 &
sysrepo_pid=$!
sleep 0.5

# Iterate over all .sh files in the current directory
for test_script in tests/*.sh; do
    # Check if the file is executable and has the .sh extension
    if [ -x "$test_script" ] && [[ "$test_script" == *.sh ]]; then
        echo "#############################################"
        echo "#####  Running case $test_script"
        echo "#############################################"
        ./"$test_script"
        script_ret=$?  # Capture the return value of the test script
        if [ $script_ret -ne 0 ]; then
            echo "Error: $test_script failed with exit code $script_ret"
            ret=$script_ret  # Update the overall return value if any test fails
        fi
        echo "Completed $test_script"
    fi
done

## cleanup
kill $sysrepo_pid
wait $sysrepo_pid

# Check if the previous steps encountered any errors
if [ $ret -ne 0 ]; then
    echo "Error: One or more test scripts failed."
    exit $ret
fi

# Exit with return value
exit $ret