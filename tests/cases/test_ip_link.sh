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
sysrepocfg -d running --edit  tests/cases/test_ip_link_data.xml || ret=$?
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

# Step 4: Check if IP vlan10 is created
if ip link show vlan10 >/dev/null 2>&1; then
    echo "TEST-INFO: IP link vlan10 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP link vlan10 (FAIL)"
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

# Step 3: Check if the qos-map for IP vlan10 is updated by iproute2-sysrepo
sysrepocfg -S '/iproute2-ip-link:links/link[name="vlan10"]/iproute2-ip-link-vlan-ext:vlan/egress-qos-map' --value 10:31

# Step 4: Run the ip command and extract the egress-qos-map
# Run the ip command and extract the egress-qos-map
egress_qos_map=$(ip -d link show vlan10 | grep -oP 'egress-qos-map \{\s*\K[^\}]+')

# Check if egress_qos_map is empty
if [ -z "$egress_qos_map" ]; then
    echo "TEST-ERROR: Failed to retrieve egress-qos-map for vlan10"
    exit 1
fi

# Check if the egress-qos-map contains the expected value "10:7"
if [[ "$egress_qos_map" =~ "10:7" ]]; then
    echo "TEST-INFO: egress-qos-map updated on vlan10  (OK)"
else
    echo "TEST-ERROR: egress-qos-map for vlan10 is incorrect (FAIL)"
    exit 1
fi
####################################################################
# Test: Delete IP Links
####################################################################
echo "--------------------"
echo "[3] Test Link DELETE"
echo "---------------------"

# step 1: work arround to delete the vlan10 first
sysrepocfg -C  tests/cases/test_ip_link_data2.xml -d running
sleep 0.1
sysrepocfg -d running --edit  tests/cases/test_ip_link_data2.xml || ret=$?

# Step 2: delete testif0 and testif1 from sysrepo
sysrepocfg -C startup -d running -m iproute2-ip-link || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to delete IP links from sysrepo"
    exit "$ret"
fi

# Step 3: check if interface deleted by iproute2-sysrepo
if ! ip link show testIf0 >/dev/null 2>&1 && ! ip link show testIf1 >/dev/null 2>&1; then
    echo "TEST-INFO: IP links testIf0 and testIf1 are deleted successfully (OK)"
else
    echo "TEST-ERROR: Failed to delete IP links testIf0 and testIf1 (FAIL)"
    exit 1
fi

####################################################################
# Test: test ip link vrf.
####################################################################

echo "-----------------"
echo "[2] Test Link VRF"
echo "-----------------"

# Step 1: add vrf vpn10 and add link testvrf and assign vrf vpn10 to it.

sysrepocfg -C  tests/cases/test_ip_link_data4.xml -d running
sleep 0.1

# Step 2: Check if the master for IP testvrf is updated by iproute2-sysrepo
master_value=$(ip link show dev testvrf 2>/dev/null | grep -oP '(?<=master )\w+' | head -n 1)

if [ -z "$master_value" ]; then
    echo "TEST-ERROR: Failed to retrieve master_value for IP link testvrf (FAIL)"
    exit 1
fi

# Step 3: Check if the master has the expected value "vpn10"
if [[ "$master_value" =~ "vpn10" ]]; then
    echo "TEST-INFO: vrf vpn10 set successfully for link  testvrf (OK)"
else
    echo "TEST-ERROR: failed to set vrf vpn10 value for link  testvrf (FAIL)"
    exit 1
fi

# cleanup
sysrepocfg -C startup -d running -m iproute2-ip-link



####################################################################
# Test: test ip link bond.
####################################################################

echo "------------------"
echo "[2] Test Link bond"
echo "------------------"

# Step 1: create two links eth_b0 and eth_b1 and add them to bond0.

sysrepocfg -C  tests/cases/test_ip_link_data5.xml -d running
sleep 0.1

# Step 2: Check if  eth_b0, eth_b1 are created
if ip link show eth_b0 >/dev/null 2>&1 && ip link show eth_b1 >/dev/null 2>&1 ; then
    echo "TEST-INFO: IP link eth_b0 and eth_b1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP link eth_b0 and eth_b1"
    exit 1
fi

# Step 3: Check if eth_b0 have bond0 as master
master_value=$(ip link show dev eth_b0 2>/dev/null | grep -oP '(?<=master )\w+' | head -n 1)

if [ -z "$master_value" ]; then
    echo "TEST-ERROR: Failed to retrieve master_value for IP link eth_b0 (FAIL)"
    exit 1
fi

# Step 3: Check if the master has the expected value "bond0"
if [[ "$master_value" =~ "bond0" ]]; then
    echo "TEST-INFO: eth_b0 master set successfully to bond0 (OK)"
else
    echo "TEST-ERROR: failed to set bond0 as master to link eth_b0 (FAIL)"
    exit 1
fi
# cleanup
sysrepocfg -C startup -d running -m iproute2-ip-link

# Step 3: Check if all links were deleted
if ! ip link show bond0 >/dev/null 2>&1 && ! ip link show eth_b0 >/dev/null 2>&1 && ! ip link show eth_b1 >/dev/null 2>&1; then
    echo "TEST-INFO: bond0, eth_b0 and eth_b1 deleted successfully (OK)"
else
    echo "TEST-ERROR: failed to delete bond0, eth_b0 or eth_b1 (FAIL)"
    exit 1
fi

####################################################################
# Test: test rollback on failure.
####################################################################

echo "----------------------"
echo "[2] Test Link ROLLBACK"
echo "----------------------"

# Step 1: create a interface using ip link directly
ip link add testIf1 type dummy

# Step 2: create a 2 dummy interface using ip route2
# one of the two is "testIf1" which created previously.
# this will trigger "RTNETLINK answers: File exists" error
# and the other interface should be deleted by rollback.

sysrepocfg -d running --edit  tests/cases/test_ip_link_data2.xml >/dev/null 2>&1

# Step 3: make sure the created interface is deleted.
if ! ip link show testIf0 >/dev/null 2>&1; then
   echo "TEST-INFO: rollback successful, interface testIf0 deleted successfully (OK)"
else
   echo "TEST-ERROR: rollback failed, interface testIf0 still exist (FAIL)"
   exit 1
fi

# Step 4: cleanup
ip link del testIf1

# ## test rollback update
#
## Step 1: add testIf0 and testIf1
sysrepocfg -d running --edit  tests/cases/test_ip_link_data2.xml || ret=$?
sleep 0.1

# Step 2: delete testIf1 manually
ip link del testIf1

# Step 3: update MTU on testIf0 and delete testIf1 from iproute2-sysrepo (expected to fail and rull back mtu to 1400)
sysrepocfg -d running --edit  tests/cases/test_ip_link_data2.xml  >/dev/null 2>&1

# Step 4: check the MTU is reverted back to 1400
current_mtu=$(ip link show dev testIf0 2>/dev/null | grep -oP '(?<=mtu )\d+' | head -n 1)

if [ -z "$current_mtu" ]; then
    echo "TEST-ERROR: Failed to retrieve MTU for IP link testIf0"
    exit 1
fi

if [ "$current_mtu" -eq 1400 ]; then
    echo "TEST-INFO: Rollback successful, MTU for IP link testIf0 is 1400 (OK)"
else
    echo "TEST-ERROR: Rollback Failed, MTU for IP link testIf0 was not reverted back to 1400 (FAIL)"
    exit 1
fi



# Step 5: cleanup
ip link add testIf1 type dummy
sysrepocfg -C startup -d running -m iproute2-ip-link
sleep 0.1


# Exit with return value
exit $ret
