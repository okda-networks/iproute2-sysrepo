#!/bin/bash

#####################################################################
# Testbed Script for Testing iproute2-sysrepo "ip link" functionality
#####################################################################
# This script performs a series of tests on the iproute2-sysrepo
# functionality related to IP link manipulation. It verifies the
# creation, deletion, and updating of IP links by iproute2-sysrepo
# using sysrepocfg commands and checks if the operations are
# successful.
#
# Test Steps:
# 1. Test creating link
# 2. Test updating link
# 3. Test deleting link
#####################################################################

ret=0
####################################################################
# Test: Create IP Links
####################################################################
echo "--------------------"
echo "[1] Test Link CREATE"
echo "---------------------"

# Step 1: Add IP links to RUNNING data store
sysrepocfg -d running --edit  tests/test_ip_link_data.xml || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to create links in sysrepo datastore"
    exit "$ret"
fi

# Step 2: Check if IP testIf0 is created
if ip link show testIf0 >/dev/null 2>&1; then
    echo "TEST-INFO: IP link testIf0 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP link testIf0"
    exit 1
fi

# Step 3: Check if IP testIf1 is created
if ip link show testIf1 >/dev/null 2>&1; then
    echo "TEST-INFO: IP link testIf1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP link testIf1 (FAIL)"
    exit 1
fi
sleep 0.2
####################################################################
# Test: Update IP Links
####################################################################
echo "--------------------"
echo "[2] Test Link UPDATE"
echo "---------------------"

# Step 1: update MTU in sysrepo
sysrepocfg -S '/iproute2-ip-link:links/link[name="testIf0"]/mtu' --value  1400

# Step 2: Check if the MTU for IP testIf0 is updated by iproute2-sysrepo
current_mtu=$(ip link show dev testIf0 2>/dev/null | grep -oP '(?<=mtu )\d+' | head -n 1)

if [ -z "$current_mtu" ]; then
    echo "TEST-ERROR: Failed to retrieve MTU for IP link testIf0"
    exit 1
fi

if [ "$current_mtu" -eq 1400 ]; then
    echo "TEST-INFO: MTU for IP link testIf0 updated successfully (OK)"
else
    echo "TEST-ERROR: Failed to update MTU for IP link testIf0 (FAIL)"
    exit 1
fi

####################################################################
# Test: Delete IP Links
####################################################################
echo "--------------------"
echo "[3] Test Link DELETE"
echo "---------------------"

# Step 1: delete data from sysrepo
sysrepocfg -C startup -d running -m iproute2-ip-link || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to delete IP links"
    exit "$ret"
fi

# Step 2: check if interface deleted by iproute2-sysrepo
if ! ip link show testIf0 >/dev/null 2>&1 && ! ip link show testIf1 >/dev/null 2>&1; then
    echo "TEST-INFO: IP links testIf0 and testIf1 are deleted successfully"
else
    echo "TEST-ERROR: Failed to delete IP links testIf0 and testIf1"
    exit 1
fi


# Exit with return value
exit $ret
