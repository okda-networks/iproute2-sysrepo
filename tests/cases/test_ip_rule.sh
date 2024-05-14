#!/bin/bash

#####################################################################
# Testbed Script for Testing iproute2-sysrepo "ip rule" functionality
#####################################################################
# This script performs a series of tests on the iproute2-sysrepo
# functionality related to IP rule manipulation. It verifies the
# creation, deletion, and updating of IP rule by iproute2-sysrepo
# using sysrepocfg commands and checks if the operations are
# successful.
#
# Test Steps:
# 1. Test creating rule
# 2. Test updating rule
# 3. Test deleting rule
#####################################################################
clean_up(){
  ip rule del pref 200 from 1.1.1.1/32 2>/dev/null
  ip rule del pref 201 to 1.1.1.1/32 2>/dev/null
}

ret=0
####################################################################
# Test: Create IP rule
####################################################################
echo "--------------------"
echo "[1] Test rule CREATE"
echo "---------------------"

# Step 1: Add IP rule to RUNNING data store
sysrepocfg -d running --edit  tests/cases/test_ip_rule_data.xml || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to create rules in sysrepo datastore"
    clean_up
    exit "$ret"
fi

# Step 2: Check if first rule is created
if ip rule show pref 200 from 1.1.1.1/32 | grep -q 'from 1.1.1.1'; then
    echo "TEST-INFO: rule from 1.1.1.1/32 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create rule from 1.1.1.1/32 (FAIL)"
    clean_up
    exit 1
fi


# Step 3: Check if second rule is created
if ip rule show pref 201 to 1.1.1.1/32  | grep -q 'to 1.1.1.1'; then
    echo "TEST-INFO: rule to 1.1.1.1/32 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create rule to 1.1.1.1/32 (FAIL)"
    clean_up
    exit 1
fi

####################################################################
# Test: Update IP rule
####################################################################
echo "--------------------"
echo "[1] Test rule UPDATE"
echo "---------------------"
ip rule show 11.11.11.11/32
# Step 1: change table
sysrepocfg -S '/iproute2-ip-rule:rules/rule[pref="201"][from="0.0.0.0/0"][to="1.1.1.1/32"][tos="default"][fwmark="0x00"]/action/table' --value  100

# Step 3: Capture table id for the rule
table_id=$(ip rule show pref 201 to 1.1.1.1/32 | grep -oP '(?<=lookup )\d+' | head -n 1)

# Step 4: Check if the rule table was updated to 100
if [ -z "$table_id" ]; then
    echo "TEST-ERROR: Failed to retrieve table_id for rule to 1.1.1.1/32"
    clean_up
    exit 1
fi

if [ "$table_id" -eq 100 ]; then
    echo "TEST-INFO: table_id for rule to 1.1.1.1/32 updated successfully (OK)"
else
    echo "TEST-ERROR: Failed to update table_id for rule to 1.1.1.1/32, table_id = $table_id (FAIL)"
    clean_up
    exit 1
fi

####################################################################
# Test: Delete IP rule
####################################################################
echo "--------------------"
echo "[1] Test rule DELETE"
echo "---------------------"

# Step 1: delete the rules from sysrepo
sysrepocfg -C startup -d running || ret=$?

# Attempt to delete the first rule
ip rule del pref 200 from 1.1.1.1/32 2>/dev/null

# if cmd exist failed (no such process) then the rule deleted successfully.
if [ $? -ne 0 ]; then
    echo "Rule from 1.1.1.1/32 deleted successfully (OK)"
else
    echo "Failed to delete rule from 1.1.1.1/32  (FAIL)"
    clean_up
    exit 1
fi

# Attempt to delete the second rule
ip rule del pref 201 to 1.1.1.1/32 2>/dev/null

# if cmd exist failed (no such process) then the rule deleted successfully.
if [ $? -ne 0 ]; then
    echo "Rule to 1.1.1.1/32  deleted successfully (OK)"
else
    echo "Failed to delete rule to 1.1.1.1/32  (FAIL)"
    clean_up
    exit 1
fi

exit $ret