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
if_clean_up(){
  ip link del vti_if1
  ip link del br1
  ip link del testIf0
  ip link del testIf1
  ip link del testIf2
  ip link del testIf121
  ip link del vxlan20
  ip link del gre3
}

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
    if_clean_up
    exit 1
fi

# Step 3: Check if IP testIf1 is created
if ip link show testIf1 >/dev/null 2>&1; then
    echo "TEST-INFO: IP link testIf1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP link testIf1 (FAIL)"
    if_clean_up
    exit 1
fi

# Step 4: Check if IP vlan10 is created
if ip link show vlan10 >/dev/null 2>&1; then
    echo "TEST-INFO: IP link vlan10 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP link vlan10 (FAIL)"
    if_clean_up
    exit 1
fi

# Step 5: Check if IP vti_if1 is created
if ip link show vti_if1 >/dev/null 2>&1; then
    echo "TEST-INFO: IP link vti_if1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP link vti_if1 (FAIL)"
    if_clean_up
    exit 1
fi

# Step 6: Check if bridge br1 is created
if ip link show br1 >/dev/null 2>&1; then
    echo "TEST-INFO: bridge link br1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create bridge br1 (FAIL)"
    if_clean_up
    exit 1
fi

# Step : check if testfIf121 is added with correct vlans
vid=$(bridge vlan show dev testIf121 2>/dev/null | grep '\b1011\b' | awk '{print $1}')
if [ -z "$vid" ]; then
    echo "TEST-ERROR: Failed to find VLAN vid 1011 for link testIf121"
    # if_clean_up   # Uncomment if you have this function defined
    exit 1
fi

# Step 6: Check if vxlan vxlan20 is created
if ip link show vxlan20 >/dev/null 2>&1; then
    echo "TEST-INFO: vxlan link vxlan20 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create vxlan vxlan20 (FAIL)"
    if_clean_up
    exit 1
fi

# Step 6: Check if gre gre3 is created
if ip link show gre3 >/dev/null 2>&1; then
    echo "TEST-INFO: gre link gre3 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create gre gre3 (FAIL)"
    if_clean_up
    exit 1
fi

sleep 0.2
####################################################################
# Test: Update IP Links
####################################################################
echo "--------------------"
echo "[2] Test Link UPDATE"
echo "---------------------"

# Step 1: update MTU on testIf0 in sysrepo
sysrepocfg -S '/iproute2-ip-link:links/link[name="testIf0"]/mtu' --value  1400

# Step 2: Check if the MTU for IP testIf0 is updated by iproute2-sysrepo
current_mtu=$(ip link show dev testIf0 2>/dev/null | grep -oP '(?<=mtu )\d+' | head -n 1)

if [ -z "$current_mtu" ]; then
    echo "TEST-ERROR: Failed to retrieve MTU for IP link testIf0"
    if_clean_up
    exit 1
fi

if [ "$current_mtu" -eq 1400 ]; then
    echo "TEST-INFO: MTU for IP link testIf0 updated successfully (OK)"
else
    echo "TEST-ERROR: Failed to update MTU for IP link testIf0, current_mtu = $current_mtu (FAIL)"
    if_clean_up
    exit 1
fi

# Step 3: add ip address 5.5.5.5/24
sysrepocfg -S '/iproute2-ip-link:links/link[name="testIf0"]/ip[address="5.5.5.5/24"]' --value n

# Step 4: check ip address added
current_ip4=($(ip address show testIf0 2>/dev/null | awk '/inet / {print $2}' | head -n 2))
current_ip6=$(ip address show testIf0 2>/dev/null | awk '/inet6 / {print $2}' | head -n 1)

# Step 5: check if both ips added to testIf0
if [ "${current_ip4[0]}" = "4.4.4.4/32" -a "${current_ip4[1]}" = "5.5.5.5/24" ]; then
    echo "TEST-INFO:  IPv4 link testIf0 updated successfully (OK)."
else
    echo "TEST-ERROR: Failed to update IPv4 for IP link testIf0, current_ip[0]=${current_ip[0]}, current_ip[1]=${current_ip[1]} (FAIL)"
    if_clean_up
    exit 1
fi

if [ "${current_ip6}" = "2001:bd8::11/64" ]; then
    echo "TEST-INFO:  IPv6 was set properly to link testIf0 (OK)."
else
    echo "TEST-ERROR: Failed to get IPv6 for link testIf0, current_ip6=${current_ip6} (FAIL),
     current_ip[2]="${current_ip[2]}" "
    if_clean_up
    exit 1
fi

# Step 3: Check if the qos-map for IP vlan10 is updated by iproute2-sysrepo
sysrepocfg -S '/iproute2-ip-link:links/vlan[name="vlan10"]/vlan-info/egress-qos-map' --value 10:31

# Step 4: Run the ip command and extract the egress-qos-map
# Run the ip command and extract the egress-qos-map
egress_qos_map=$(ip -d link show vlan10 | grep -oP 'egress-qos-map \{\s*\K[^\}]+')

# Check if egress_qos_map is empty
if [ -z "$egress_qos_map" ]; then
    echo "TEST-ERROR: Failed to retrieve egress-qos-map for vlan10"
    if_clean_up
    exit 1
fi

# Check if the egress-qos-map contains the expected value "10:7"
if [[ "$egress_qos_map" =~ "10:7" ]]; then
    echo "TEST-INFO: egress-qos-map updated on vlan10  (OK)"
else
    echo "TEST-ERROR: egress-qos-map for vlan10 is incorrect, egress-qos-map = $egress_qos_map (FAIL)"
    if_clean_up
    exit 1
fi

# Step 5: update ikey on vti_if1

sysrepocfg -S '/iproute2-ip-link:links/vti[name="vti_if1"]/tunnel-info/ikey' --value  0.0.0.100

# Step 6: check if ikey value updated
ikey_value=$(ip -d link show dev vti_if1 2>/dev/null | grep -oP '(?<=ikey )\d+.\d+.\d+.\d+' | head -n 1)

if [ "$ikey_value" = "0.0.0.100" ]; then
    echo "TEST-INFO: ikey for vti vti_if1 updated successfully (OK)"
else
    echo "TEST-ERROR: Failed to update ikey for vti vti_if1, ikey_value = $ikey_value (FAIL)"
    if_clean_up
    exit 1
fi

####################################################################
# Test: Delete IP Links
####################################################################
echo "--------------------"
echo "[3] Test Link DELETE"
echo "---------------------"

# Step 1: delete vlan10, testif0 and testif1 from sysrepo
sysrepocfg -C startup -d running -m iproute2-ip-link || ret=$?
# Check if sysrepocfg command failed
if [ -n "$ret" ] && [ "$ret" -ne 0 ]; then
    echo "TEST-ERROR: failed to delete IP links from sysrepo"
    if_clean_up
    exit "$ret"
fi

# Step 2: check if interface deleted by iproute2-sysrepo
if ! ip link show testIf0 >/dev/null 2>&1 && ! ip link show testIf1 >/dev/null 2>&1; then
    echo "TEST-INFO: IP links testIf0 and testIf1 are deleted successfully (OK)"
else
    echo "TEST-ERROR: Failed to delete IP links testIf0 and testIf1 (FAIL)"
    if_clean_up
    exit 1
fi

# Step 2: check if interface deleted by iproute2-sysrepo
if ! ip link show vti_if1 >/dev/null 2>&1 ; then
    echo "TEST-INFO: IP links testIf0 and vti_if1 are deleted successfully (OK)"
else
    echo "TEST-ERROR: Failed to delete IP links testIf0 and vti_if1 (FAIL)"
    if_clean_up
    exit 1
fi

####################################################################
# Test: test ip link vrf.
####################################################################

echo "-----------------"
echo "[4] Test Link VRF"
echo "-----------------"

# Step 1: add vrf vpn10 and add link testvrf and assign vrf vpn10 to it.

sysrepocfg --edit  tests/cases/test_ip_link_data4.xml -d running
sleep 0.1

# Step 2: Check if the master for IP testvrf is updated by iproute2-sysrepo
master_value=$(ip link show dev testvrf 2>/dev/null | grep -oP '(?<=master )\w+' | head -n 1)

if [ -z "$master_value" ]; then
    echo "TEST-ERROR: Failed to retrieve master_value for IP link testvrf (FAIL)"
    if_clean_up
    exit 1
fi

# Step 3: Check if the master has the expected value "vpn10"
if [[ "$master_value" =~ "vpn10" ]]; then
    echo "TEST-INFO: vrf vpn10 set successfully for link  testvrf (OK)"
else
    echo "TEST-ERROR: failed to set vrf vpn10 value for link  testvrf (FAIL)"
    if_clean_up
    exit 1
fi

# cleanup
sysrepocfg -C startup -d running -m iproute2-ip-link



####################################################################
# Test: test ip link bond.
####################################################################

echo "------------------"
echo "[5] Test Link bond"
echo "------------------"

# Step 1: create two links eth_b0 and eth_b1 and add them to bond0.

sysrepocfg --edit  tests/cases/test_ip_link_data5.xml -d running
sleep 0.1

# Step 2: Check if  eth_b0, eth_b1 are created
if ip link show eth_b0 >/dev/null 2>&1 && ip link show eth_b1 >/dev/null 2>&1 ; then
    echo "TEST-INFO: IP link eth_b0 and eth_b1 created successfully (OK)"
else
    echo "TEST-ERROR: Failed to create IP link eth_b0 and eth_b1"
    if_clean_up
    exit 1
fi

# Step 3: Check if eth_b0 have bond0 as master
master_value=$(ip link show dev eth_b0 2>/dev/null | grep -oP '(?<=master )\w+' | head -n 1)

if [ -z "$master_value" ]; then
    echo "TEST-ERROR: Failed to retrieve master_value for IP link eth_b0 (FAIL)"
    if_clean_up
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
echo "[6] Test Link ROLLBACK"
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
    echo "TEST-ERROR: Rollback Failed, MTU for IP link testIf0 was not reverted back to 1400, current_mtu = $current_mtu (FAIL)"
    exit 1
fi



# Step 5: cleanup
ip link add testIf1 type dummy
sysrepocfg -C startup -d running -m iproute2-ip-link
sleep 0.1


# Exit with return value
exit $ret
