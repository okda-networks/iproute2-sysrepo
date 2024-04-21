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
  ip link del if_tc3 2>/dev/null
  ip link del if_tc4 2>/dev/null
  ip link del if_tc5 2>/dev/null
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

# Step 2: Check if clsact qdisc created
if tc qdisc show dev if_tc1 clsact >/dev/null 2>&1; then
    echo "TEST-INFO: clsact qdisc for if_tc1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create clsact qdisc for if_tc1 (FAIL)"
    clean_up
    exit 1
fi

# Step 3: Check if ingress qdisc is created
if tc qdisc show dev if_tc2 ingress >/dev/null 2>&1; then
    echo "TEST-INFO: ingress qdisc for if_tc2  created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create ingress qdisc for if_tc2 (FAIL)"
    clean_up
    exit 1
fi

# Step 4: Check if pfifo qdisc created
if tc qdisc show dev if_tc3 root >/dev/null 2>&1; then
    echo "TEST-INFO: pfifo qdisc for if_tc3 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create pfifo qdisc for if_tc3 (FAIL)"
    clean_up
    exit 1
fi

# Step 5: Check if fq_codel qdisc is created
if tc qdisc show dev if_tc4 root >/dev/null 2>&1; then
    echo "TEST-INFO: fq_codel qdisc for if_tc4  created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create fq_codel qdisc for if_tc4 (FAIL)"
    clean_up
    exit 1
fi

# Step 6: Check if tbf qdisc is created
if tc qdisc show dev if_tc5 root >/dev/null 2>&1; then
    echo "TEST-INFO: tbf qdisc for if_tc5  created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create tbf qdisc for if_tc5 (FAIL)"
    clean_up
    exit 1
fi

####################################################################
# Test: Delete IP qdisc
####################################################################
echo "--------------------"
echo "[1] Test qdisc DELETE"
echo "---------------------"

# Step 1: delete the qdisc from sysrepo
sysrepocfg -C startup -d running -m iproute2-tc-qdisc || ret=$?

# Attempt to delete the clsact qdisc
tc qdisc del dev if_tc1 clsact 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "clsact qdisc for if_tc1 deleted successfully (OK)"
else
    echo "Failed to delete clsact qdisc for if_tc1 (FAIL)"
    clean_up
    exit 1
fi

# Attempt to delete the ingress qdisc
tc qdisc del dev if_tc2 ingress 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "ingress qdisc for if_tc2 deleted successfully (OK)"
else
    echo "Failed to delete ingress qdisc for if_tc2 (FAIL)"
    clean_up
    exit 1
fi

# Attempt to delete the first qdisc
tc qdisc del dev if_tc3 root 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "pfifo qdisc for if_tc1 deleted successfully (OK)"
else
    echo "Failed to delete pfifo qdisc for if_tc1 (FAIL)"
    clean_up
    exit 1
fi

# Attempt to delete the qdisc
tc qdisc del dev if_tc4 root 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "fq_codel qdisc for if_tc2 deleted successfully (OK)"
else
    echo "Failed to delete fq_codel qdisc for if_tc2 (FAIL)"
    clean_up
    exit 1
fi

# Attempt to delete the qdisc
tc qdisc del dev if_tc5 root 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "tbf qdisc for if_tc5 deleted successfully (OK)"
else
    echo "Failed to delete tbf qdisc for if_tc5 (FAIL)"
    clean_up
    exit 1
fi

clean_up

exit $ret
