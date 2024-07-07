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
clean_up(){
  ip nexthop del id 1001
  ip nexthop del id 2001
  ip nexthop del id 3001
  ip nexthop del id 4001
  ip nexthop del id 400100
  ip -n nh_red nexthop del id 4001
  ip nexthop del id 12001
  ip netns del nh_red
}


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
    echo "TEST-ERROR:NEXTHOP: failed to create nexthops in sysrepo datastore"
    exit "$ret"
fi

# Step 2: Check if nh id 1001 is created
if ip nexthop show id 1001 >/dev/null 2>&1; then
    echo "TEST-INFO:NEXTHOP: IP nexthop id 1001 created successfully (OK)"
else
    echo "TEST-ERROR:NEXTHOP: Failed to create IP nexthop 1001 (FAIL)"
    clean_up
    exit 1
fi

# Step 3: Check if nh id 2001 is created
if ip nexthop show id 2001 >/dev/null 2>&1; then
    echo "TEST-INFO:NEXTHOP: IP nexthop id 2001 created successfully (OK)"
else
    echo "TEST-ERROR:NEXTHOP: Failed to create IP nexthop 2001 (FAIL)"
    clean_up
    exit 1
fi

# Step 4: Check if nh id 3001 is created
if ip nexthop show id 3001 >/dev/null 2>&1; then
    echo "TEST-INFO:NEXTHOP: IP nexthop id 3001 created successfully (OK)"
else
    echo "TEST-ERROR:NEXTHOP: Failed to create IP nexthop 3001 (FAIL)"
    clean_up
    exit 1
fi

# Step 5: Check if nh id 4001 is created
if ip nexthop show id 4001 >/dev/null 2>&1; then
    echo "TEST-INFO:NEXTHOP: IP nexthop id 4001 created successfully (OK)"
else
    echo "TEST-ERROR:NEXTHOP: Failed to create IP nexthop 4001 (FAIL)"
    clean_up
    exit 1
fi

# Step 6: Check if nh id 12 is created
if ip nexthop show id 12001 >/dev/null 2>&1; then
    echo "TEST-INFO:NEXTHOP: IP nexthop id 12 created successfully (OK)"
else
    echo "TEST-ERROR:NEXTHOP: Failed to create IP nexthop 2 (FAIL)"
    clean_up
    exit 1
fi

# Step 7: Check if nh id 4001 in netns red is created
if ip -n nh_red nexthop show id 400100 >/dev/null 2>&1; then
    echo "TEST-INFO:NEXTHOP: IP nexthop id 400100 in netns nh_red created successfully (OK)"
else
    echo "TEST-ERROR:NEXTHOP: Failed to create IP nexthop 400100 in netns nh_red (FAIL)"
    clean_up
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
#    echo "TEST-ERROR:NEXTHOP: Failed to retrieve via for IP nexthop 2"
#    exit 1
#fi
#
#if [ "$current_via" = "192.168.2.2" ]; then
#    echo "TEST-INFO:NEXTHOP: via for IP nexthop 2 updated successfully (OK)"
#else
#    echo "TEST-ERROR:NEXTHOP: Failed to update via for IP nexthop 2 (FAIL)"
#    exit 1
#fi

####################################################################
# Test: Delete IP nexthops
####################################################################
echo "--------------------"
echo "[3] Test nexthop DELETE"
echo "---------------------"

## step 0: work arround to delete the nexthop id 12 first
#sysrepocfg -C tests/cases/test_ip_nexthop_data2.xml -d running -m iproute2-ip-nexthop
#sleep 0.1
#sysrepocfg -d running --edit  tests/cases/test_ip_nexthop_data2.xml || ret=$?

# Step 1: delete data from sysrepo
sysrepocfg -C startup -d running || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR:NEXTHOP: failed to delete IP nexthops from sysrepo"
    clean_up
    exit "$ret"
fi

# Step 2: check if nexthops deleted by iproute2-sysrepo
if ! ip nexthop show id 1001 >/dev/null 2>&1 && ! ip nexthop show id 2001 >/dev/null 2>&1 \
  && ! ip nexthop show id 3001 >/dev/null 2>&1  && ! ip nexthop show id 12001 >/dev/null 2>&1\
   && ! ip nexthop show id 4001 >/dev/null 2>&1 ; then
    echo "TEST-INFO:NEXTHOP: IP nexthops 1001, 2001, 3001 and 12 are deleted successfully (OK)"
else
    echo "TEST-ERROR:NEXTHOP: Failed to delete IP nexthops 2001, 1001, 3001,4001 or 12001 (FAIL)"
    clean_up
    exit 1
fi

# Exit with return value
exit $ret
