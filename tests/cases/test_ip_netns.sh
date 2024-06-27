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
clean_up(){
  ip netns del vpn10
  ip netns del vpn20
  ip link del netns_if
}

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
    echo "TEST-ERROR:netns: failed to create netnss in sysrepo datastore"
    clean_up
    exit "$ret"
fi

# Step 2: Check if IP 2 is created
if ip netns add vpn10 >/dev/null 2>&1; then
    echo "TEST-ERROR:netns: Failed to create IP netns vpn10 (FAIL)"
    clean_up
    exit 1
else
    echo "TEST-INFO: IP netns vpn10 created successfully (OK)"
fi

# Step 3: Check if IP vpn20 is created
if ip netns add vpn20 >/dev/null 2>&1; then
    echo "TEST-ERROR:netns: Failed to create IP netns vpn20 (FAIL)"
    clean_up
    exit 1
else
    echo "TEST-INFO: IP netns vpn20 created successfully (OK)"
fi

sleep 0.2


# Step 4: Check if link netns_if is added to correct netns with correct ip
if ip -n vpn10 link show | grep -q netns_if; then
    echo "TEST-INFO: link netns_if added to netns vpn10 created successfully (OK)"
else
    echo "TEST-ERROR:netns: Failed to add netns_if to netns vpn10 (FAIL)"
    clean_up
    exit 1
fi

# Step 5: check ip address added
current_ip4=($(ip -n vpn10 address show netns_if 2>/dev/null | awk '/inet / {print $2}' | head -n 2))

echo "found ip = ${current_ip4[0]}"

# Step 5: check if both ips added to netns_if
if [ "${current_ip4[0]}" = "1.2.2.3/32" ]; then
    echo "TEST-INFO:  IPv4 link netns_if in netns vpn10 updated successfully (OK)."
else
    echo "TEST-ERROR:netns: Failed to add ip to link  netns_if in netns vpn10, current_ip[0]=${current_ip4[0]} (FAIL)"
    clean_up
    exit 1
fi

####################################################################
# Test: update IP link is netns
####################################################################

sysrepocfg -S '/iproute2-ip-link:links/link[name="netns_if"]/mtu' --value  1400

# Step 2: Check if the MTU for IP netns_if is updated by iproute2-sysrepo
current_mtu=$(ip link show dev netns_if 2>/dev/null | grep -oP '(?<=mtu )\d+' | head -n 1)

if [ -z "$current_mtu" ]; then
    echo "TEST-ERROR:netns: Failed to retrieve MTU for IP link netns_if "
    if_clean_up
    exit 1
fi

if [ "$current_mtu" -eq 1400 ]; then
    echo "TEST-INFO:netns: MTU for IP link netns_if updated successfully (OK)"
else
    echo "TEST-ERROR:netns: Failed to update MTU for IP link netns_if, current_mtu = $current_mtu (FAIL)"
    if_clean_up
    exit 1
fi

####################################################################
# Test: Delete IP netnss
####################################################################
echo "--------------------"
echo "[3] Test netns DELETE"
echo "---------------------"

# Step 1: delete links from sysrepo
sysrepocfg -C startup -d running -m iproute2-ip-link || ret=$?
sleep 0.3
sysrepocfg -C startup -d running -m iproute2-ip-netns || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR:netns: failed to delete IP netnss from sysrepo"
    clean_up
    exit "$ret"
fi

# Step 2: check if netns deleted by iproute2-sysrepo
if ! ip netns exec vpn10 echo >/dev/null 2>&1 && ! ip netns exec vpn20 echo >/dev/null 2>&1; then
    echo "TEST-INFO:netns: IP netns vpn10 and vpn20 are deleted successfully (OK)"
else
    echo "TEST-ERROR:netns: Failed to delete IP netns vpn10 and/or vpn20 (FAIL)"
    clean_up
    exit 1
fi



# Exit with return value
exit $ret
