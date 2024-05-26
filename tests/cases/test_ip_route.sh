#!/bin/bash

#####################################################################
# Testbed Script for Testing iproute2-sysrepo "ip route" functionality
#####################################################################
# This script performs a series of tests on the iproute2-sysrepo
# functionality related to IP route manipulation. It verifies the
# creation, deletion, and updating of IP route by iproute2-sysrepo
# using sysrepocfg commands and checks if the operations are
# successful.
#
# Test Steps:
# 1. Test creating route
# 2. Test updating route
# 3. Test deleting route
#####################################################################
clean_up(){
  ip route del 11.11.11.11/32 2>/dev/null
  ip route del 22.22.22.22/32 2>/dev/null
  ip link del if_route1 2>/dev/null
  ip link del if_route2 2>/dev/null
  ip link del if_route3 2>/dev/null
}

ret=0
####################################################################
# Test: Create IP route
####################################################################
echo "--------------------"
echo "[1] Test route CREATE"
echo "---------------------"

# Step 1: Add IP route to RUNNING data store
sysrepocfg -d running --edit  tests/cases/test_ip_route_data.xml || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to create routes in sysrepo datastore"
    clean_up
    exit "$ret"
fi

# Step 2: Check if first route is created
if ip route show 11.11.11.11/32 >/dev/null 2>&1; then
    echo "TEST-INFO: route 11.11.11.11/32 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create route 11.11.11.11/32 (FAIL)"
    clean_up
    exit 1
fi

# Step 3: Check if second route is created
if ip route show 22.22.22.22/32 >/dev/null 2>&1; then
    echo "TEST-INFO: route 22.22.22.22/32 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create route 22.22.22.22/32 (FAIL)"
    clean_up
    exit 1
fi

# github workflow is failing as mpls is not enabled.
## Step 3: Check if mpls route is created, try to create same route, if failed (device already exist) then
## the route created successfully.
#ip -M route add 10100 as 10111 dev if_route2 2>/dev/null
#
#if [ $? -ne 0 ]; then
#    echo "mpls Route 10100 created successfully (OK)"
#else
#    echo "Failed to create mpls route 10100 (FAIL)"
#    clean_up
#    exit 1
#fi

####################################################################
# Test: Update IP route
####################################################################
echo "--------------------"
echo "[1] Test route UPDATE"
echo "---------------------"
ip route show 11.11.11.11/32
# Step 1: delete nexthop opal1 from 11.11.11.11/32
sysrepocfg -d running --edit tests/cases/test_ip_route_data2.xml -m iproute2-ip-route

# Step 3: Capture next hops for 11.11.11.11/32
next_hops=($(ip route show 11.11.11.11/32 | awk '/via / {print $3}' | head -n 3))

# Step 4: Check if the route has all three next hops
if [[ "${next_hops[@]}" == "3.3.3.10 1.1.1.10 2.2.2.10" ]]; then
    echo "TEST-INFO: Route 11.11.11.11/32 has all three next hops (OK)"
else
    echo "TEST-ERROR: Route 11.11.11.11/32 does not have all three next hops (FAIL)"
    clean_up
    exit 1
fi

####################################################################
# Test: Delete IP route
####################################################################
echo "--------------------"
echo "[1] Test route DELETE"
echo "---------------------"

# Step 1: delete the routes from sysrepo
sysrepocfg -C startup -d running || ret=$?

# Attempt to delete the first route
ip route del 11.11.11.11/32 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "Route 11.11.11.11/32 deleted successfully (OK)"
else
    echo "Failed to delete route 11.11.11.11/32 (FAIL)"
    clean_up
    exit 1
fi

# Attempt to delete the second route
ip route del 22.22.22.22/32 2>/dev/null

# if cmd exist failed (no such process) then the route deleted successfully.
if [ $? -ne 0 ]; then
    echo "Route 22.22.22.22/32 deleted successfully (OK)"
else
    echo "Failed to delete route 22.22.22.22/32 (FAIL)"
    clean_up
    exit 1
fi

exit $ret