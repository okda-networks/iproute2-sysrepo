#!/bin/bash

#####################################################################
# Testbed Script for Testing iproute2-sysrepo "ip nexthop" functionality
#####################################################################
# This script performs a series of tests on the iproute2-sysrepo
# functionality related to IP nexthop manipulation. It verifies the
# creation, deletion, and updating of IP nexthops by iproute2-sysrepo
# using sysrepocfg commands and checks if the operations are
# successful.
#
# Test Steps:
# 1. Test creating nexthop
# 2. Test updating nexthop
# 3. Test deleting nexthop
#####################################################################

ret=0
####################################################################
# Test: Create IP nexthops
####################################################################
echo "--------------------"
echo "[1] Test nexthop CREATE"
echo "---------------------"

# Step 1: Add IP nexthops to RUNNING data store
sysrepocfg -d running --edit  tests/cases/test_ip_nexthop_data.xml || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to create nexthops in sysrepo datastore"
    exit "$ret"
fi

# Step 2: Check if nh id 1 is created
if ip nexthop show id 1 >/dev/null 2>&1; then
    echo "TEST-INFO: IP nexthop id 1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP nexthop 1 (FAIL)"
    exit 1
fi

# Step 3: Check if nh id 2 is created
if ip nexthop show id 2 >/dev/null 2>&1; then
    echo "TEST-INFO: IP nexthop id 2 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP nexthop 2 (FAIL)"
    exit 1
fi

# Step 4: Check if nh id 12 is created
if ip nexthop show id 12 >/dev/null 2>&1; then
    echo "TEST-INFO: IP nexthop id 12 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP nexthop 2 (FAIL)"
    exit 1
fi

sleep 0.2
####################################################################
# Test: Update IP nexthops
####################################################################
# NOTE: update require missing extension replace-on-update.
#echo "--------------------"
#echo "[2] Test nexthop UPDATE"
#echo "---------------------"
#
#ip link add name nh_test type dummy
#
## Step 1: update via in sysrepo
#sysrepocfg -S '/iproute2-ip-nexthop:nexthops/nexthop[id="2"]/ipv4/address' --value  192.168.2.2
#
## Step 2: Check if the IP for IP 2 is updated by iproute2-sysrepo
#current_via=$(ip nexthop show id 2 2>/dev/null | grep -oP '(?<=via )\d+.\d+.\d+.\d+' | head -n 1)
#
#if [ -z "$current_via" ]; then
#    echo "TEST-ERROR: Failed to retrieve via for IP nexthop 2"
#    exit 1
#fi
#
#if [ "$current_via" = "192.168.2.2" ]; then
#    echo "TEST-INFO: via for IP nexthop 2 updated successfully (OK)"
#else
#    echo "TEST-ERROR: Failed to update via for IP nexthop 2 (FAIL)"
#    exit 1
#fi

####################################################################
# Test: Delete IP nexthops
####################################################################
echo "--------------------"
echo "[3] Test nexthop DELETE"
echo "---------------------"

# step 0: work arround to delete the nexthop id 12 first
sysrepocfg -C tests/cases/test_ip_nexthop_data2.xml -d running -m iproute2-ip-nexthop 
sleep 0.1
sysrepocfg -d running --edit  tests/cases/test_ip_nexthop_data2.xml || ret=$?

# Step 1: delete data from sysrepo
sysrepocfg -C startup -d running -m iproute2-ip-nexthop || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to delete IP nexthops from sysrepo"
    exit "$ret"
fi

# Step 2: check if nexthops deleted by iproute2-sysrepo
if ! ip nexthop show id 1 >/dev/null 2>&1 && ! ip nexthop show id 2 >/dev/null 2>&1 && ! ip nexthop show id 12 >/dev/null 2>&1 ; then
    echo "TEST-INFO: IP nexthops 1, 2, 3 and 12 are deleted successfully (OK)"
else
    echo "TEST-ERROR: Failed to delete IP nexthops 2 and 1 (FAIL)"
    exit 1
fi

# Exit with return value
exit $ret
