#!/bin/bash

#####################################################################
# Testbed Script for Testing iproute2-sysrepo "tc filter" functionality
#####################################################################
# This script performs a series of tests on the iproute2-sysrepo
# functionality related to IP route manipulation. It verifies the
# creation, deletion, and updating of tc filter by iproute2-sysrepo
# using sysrepocfg commands and checks if the operations are
# successful.
#
# Test Steps:
# 1. Test creating filter
# 2. Test updating filter
# 3. Test deleting filter
#####################################################################
clean_up(){
  ip link del if_tc_f1 2>/dev/null
  ip link del if_tc_f2 2>/dev/null
  ip link del if_tc_f3 2>/dev/null
  ip link del if_tc_f4 2>/dev/null
}

ret=0
####################################################################
# Test: Create filter
####################################################################
echo "--------------------"
echo "[1] Test filter CREATE"
echo "---------------------"

# Step 1: Add filter to RUNNING data store
sysrepocfg -d running --edit  tests/cases/test_tc_filter_data.xml || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to create filter in sysrepo datastore"
    clean_up
    exit "$ret"
fi

output=$(tc filter show dev if_tc_f4 ingress)
# Step 2: Check if dev-filter created
if echo "$output" | grep -q "filter protocol ip pref 10 flower chain 0"; then
    echo "TEST-INFO: dev-filter for if_tc_f4 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create dev-filter for if_tc_f4 (FAIL)"
    clean_up
    exit 1
fi

output2=$(tc filter show block 10)
# Step 3: Check if share-block-filter is created
if echo "$output2" | grep -q "filter protocol ip pref 10 flower chain 0"; then
    echo "TEST-INFO: share-block-filter for if_tc_f3  created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create share-block-filter for if_tc_f3 (FAIL)"
    clean_up
    exit 1
fi

output3=$(tc filter show dev if_tc_f2)

# Step 3: Check if qdisc-filter u32 is created
if echo "$output3" | grep -q "filter parent 8002: protocol ip pref 20 u32 chain 0"; then
    echo "TEST-INFO: qdisc-filter (u32) for if_tc_f1  created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create qdisc-filter (u32) for if_tc_f1 (FAIL)"
    clean_up
    exit 1
fi


output4=$(tc filter show dev if_tc_f1)
# Step 3: Check if qdisc-filter flower is created
if echo "$output4" | grep -q "filter parent 8001: protocol ip pref 20 flower chain 0"; then
    echo "TEST-INFO: qdisc-filter (flower) for if_tc_f1  created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create qdisc-filter (flower) for if_tc_f1 (FAIL)"
    clean_up
    exit 1
fi

####################################################################
# Test: Update filter
####################################################################

# TODO: add update

####################################################################
# Test: Delete tc filter
####################################################################
echo "--------------------"
echo "[1] Test filter DELETE"
echo "---------------------"

# Step 1: delete the filter from sysrepo
sysrepocfg -C startup -d running || ret=$?

output=$(tc filter show dev if_tc_f4 ingress 2>/dev/null)
# Step 2: Check if dev-filter is still there
if echo "$output" | grep -q "filter protocol ip pref 10 flower chain 0"; then
    echo "TEST-ERROR: Failed to delete dev-filter for if_tc_f4 (FAIL)"
    clean_up
    exit 1
else
    echo "TEST-INFO: dev-filter for if_tc_f4 deleted successfully (OK)"
fi

output2=$(tc filter show block 10 2>/dev/null)
# Step 3: Check if share-block-filter is still there
if echo "$output2" | grep -q "filter protocol ip pref 10 flower chain 0"; then
    echo "TEST-ERROR: Failed to delete share-block-filter for if_tc_f3 (FAIL)"
    clean_up
    exit 1
else
    echo "TEST-INFO: share-block-filter for if_tc_f1  deleted successfully (OK)"
fi

output3=$(tc filter show dev if_tc_f2 2>/dev/null)

# Step 3: Check if qdisc-filter u32 is deleted
if echo "$output3" | grep -q "filter parent 8002: protocol ip pref 20 u32 chain 0"; then
   echo "TEST-ERROR: Failed to create qdisc-filter (u32) for if_tc_f1 (FAIL)"
   clean_up
   exit 1
else
  echo "TEST-INFO: qdisc-filter (u32) for if_tc_f1  created successfully (OK)"

fi


output4=$(tc filter show dev if_tc_f1 2>/dev/null)
# Step 3: Check if qdisc-filter flower is created
if echo "$output4" | grep -q "filter parent 8001: protocol ip pref 20 flower chain 0"; then
    echo "TEST-ERROR: Failed to create qdisc-filter (flower) for if_tc_f1 (FAIL)"
    clean_up
    exit 1
else
    echo "TEST-INFO: qdisc-filter (flower) for if_tc_f1  created successfully (OK)"

fi

# delete the Qdiscs and links
sysrepocfg -C startup -d running -m iproute2-tc-qdisc || ret=$?
sysrepocfg -C startup -d running -m iproute2-ip-link || ret=$?

exit $ret
