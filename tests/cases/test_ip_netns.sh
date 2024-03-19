#!/bin/bash

#####################################################################
# Testbed Script for Testing iproute2-sysrepo "ip netns" functionality
#####################################################################
# This script performs a series of tests on the iproute2-sysrepo
# functionality related to IP netns manipulation. It verifies the
# creation and deletion (netns has no update) of IP netnss by iproute2-sysrepo
# using sysrepocfg commands and checks if the operations are
# successful.
#
# Test Steps:
# 1. Test creating netns
# 2. Test deleting netns
#####################################################################

ret=0
####################################################################
# Test: Create IP netnss
####################################################################
echo "--------------------"
echo "[1] Test netns CREATE"
echo "---------------------"

# Step 1: Add IP netnss to RUNNING data store
sysrepocfg -d running --edit  tests/cases/test_ip_netns_data.xml || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to create netnss in sysrepo datastore"
    exit "$ret"
fi

# Step 2: Check if IP 2 is created
if ip netns exec vpn10 echo >/dev/null 2>&1; then
    echo "TEST-INFO: IP netns vpn10 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP netns vpn10 (FAIL)"
    exit 1
fi

# Step 3: Check if IP testIf1 is created
if ip netns exec vpn20 echo >/dev/null 2>&1; then
    echo "TEST-INFO: IP netns vpn20 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP netns vpn20 (FAIL)"
    exit 1
fi
sleep 0.2

####################################################################
# Test: Delete IP netnss
####################################################################
echo "--------------------"
echo "[3] Test netns DELETE"
echo "---------------------"

# Step 1: delete data from sysrepo
sysrepocfg -C startup -d running -m iproute2-ip-netns || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to delete IP netnss from sysrepo"
    exit "$ret"
fi

# Step 2: check if interface deleted by iproute2-sysrepo
if ! ip netns exec vpn10 echo >/dev/null 2>&1 && ! ip netns exec vpn20 echo >/dev/null 2>&1; then
    echo "TEST-INFO: IP netns vpn10 and vpn20 are deleted successfully (OK)"
else
    echo "TEST-ERROR: Failed to delete IP netns vpn10 and/or vpn20 (FAIL)"
    exit 1
fi

# Exit with return value
exit $ret
