#!/bin/bash

#####################################################################
# Testbed Script for Testing iproute2-sysrepo "tc qdisc" functionality
#####################################################################
# This script performs a series of tests on the iproute2-sysrepo
# functionality related to IP route manipulation. It verifies the
# creation, deletion, and updating of tc qdisc by iproute2-sysrepo
# using sysrepocfg commands and checks if the operations are
# successful.
#
# Test Steps:
# 1. Test creating qdisc
# 2. Test updating qdisc
# 3. Test deleting qdisc
#####################################################################
clean_up(){
  ip link del if_tc1 2>/dev/null
  ip link del if_tc2 2>/dev/null
  ip tc qdisc del dev if_tc1 clsact 2>/dev/null
  ip tc qdisc del dev if_tc2 ingress 2>/dev/null
}

ret=0
####################################################################
# Test: Create IP route
####################################################################
echo "--------------------"
echo "[1] Test qdisc CREATE"
echo "---------------------"

# Step 1: Add IP route to RUNNING data store
sysrepocfg -d running --edit  tests/cases/test_tc_qdisc_data.xml || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to create qdisc in sysrepo datastore"
    clean_up
    exit "$ret"
fi

# Step 2: Check if qdisc created
if tc qdisc show dev if_tc1 clsact >/dev/null 2>&1; then
    echo "TEST-INFO: qdisc for if_tc1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create qdisc for if_tc1 (FAIL)"
    clean_up
    exit 1
fi

# Step 3: Check if second qdisc is created
if tc qdisc show dev if_tc2 ingress >/dev/null 2>&1; then
    echo "TEST-INFO: qdisc for if_tc2  created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create qdisc for if_tc2 (FAIL)"
    clean_up
    exit 1
fi

####################################################################
# Test: Delete IP qdisc
####################################################################
echo "--------------------"
echo "[1] Test qdisc DELETE"
echo "---------------------"

# Step 1: delete the routes from sysrepo
sysrepocfg -C startup -d running -m iproute2-tc-qdisc || ret=$?

# Attempt to delete the first qdisc
tc qdisc del dev if_tc1 clsact 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "qdisc for if_tc1 deleted successfully (OK)"
else
    echo "Failed to delete qdisc for if_tc1 (FAIL)"
    clean_up
    exit 1
fi

# Attempt to delete the second route
tc qdisc del dev if_tc2 ingress 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "qdisc for if_tc2 deleted successfully (OK)"
else
    echo "Failed to delete qdisc for if_tc2 (FAIL)"
    clean_up
    exit 1
fi
