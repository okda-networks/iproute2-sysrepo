#!/bin/bash

# Initialize default variables
ret=0
operation="add"

# Interface configuration
link_name="oper_link"
link_address_v4_1="10.0.0.1/32"
link_address_v4_2="192.1.1.0/24"
link_address_v6_1="2002:bb9::20/64"
mtu_value="1450"
vlan_name="oper_vlan"
vlan_id="10"
vti_name="oper_vti"
vti_local="10.1.2.3"
vti_remote="10.10.20.30"

# Function to create LINK
create_interfaces() {
    ip link $operation name $1 mtu $2 type $3
    echo "created interface $1"
}

# Function to set link ip address
add_dev_address() {
    ip address add dev $1 $2
    echo "Added address $2 to interface $1"
}

# Function to create VLAN
create_vlan() {
    ip link $operation name $1 mtu $2 link $3 type vlan protocol 802.1q id $4
    echo "created interface $1"
}

# Function to create VTI
create_vti() {
    ip link $operation name $1 mtu $2 type vti local $3 remote $4
    echo "created interface $1"
}

# Function to delete network interfaces
delete_interfaces() {
    operation="del"
    ip link $operation name $vlan_name
    ip link $operation name $link_name
    ip link $operation name $vti_name
    
}

echo "#############################################"
echo "## Running system startup config load test ##"
echo "#############################################"

echo -e "\nCREATING INTERFACES ON LINUX"
# Create dummy, VLAN, and VTI interfaces with specified attributes
create_interfaces $link_name $mtu_value "dummy"
add_dev_address $link_name $link_address_v4_1
add_dev_address $link_name $link_address_v6_1
add_dev_address $link_name $link_address_v4_2
create_vlan $vlan_name $mtu_value $link_name $vlan_id
create_vti $vti_name $mtu_value $vti_local $vti_remote

# Run iproute2-sysrepo and store its PID
echo -e "\nSTARTING IPROUTE2-SYSREPO"
./bin/iproute2-sysrepo 2>&1 &
sysrepo_pid=$!
sleep 0.5

# Function to cleanup
cleanup() {
  delete_interfaces
  kill $sysrepo_pid
  wait $sysrepo_pid  
}

# Function to check interface configuration in sysrepo
check_interface() {
    echo -e "- Checking interface ($1) configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-ip-link:links/link[name=\"$1\"]")
    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)."
        cleanup
        exit 1
    fi

    echo "Checking name field"
    if ! echo "$output" | grep -qP "<name>\s*$1\s*</name>"; then
        echo "Error: Name $1 not found (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking mtu field"
    if ! echo "$output" | grep -qP "<mtu>\s*$mtu_value\s*</mtu>"; then
        echo "Error: MTU value $mtu_value for $1 not found (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking address field"
    if ! echo "$output" | grep -qP "<address>\s*$link_address_v4_1\s*</address>"; then
        echo "Error: Address $link_address_v4_1 not found (FAIL)."
        cleanup
        exit 1
    fi
    if ! echo "$output" | grep -qP "<address>\s*$link_address_v4_2\s*</address>"; then
        echo "Error: Address $link_address_v4_2 not found (FAIL)."
        cleanup
        exit 1
    fi
    if ! echo "$output" | grep -qP "<address>\s*$link_address_v6_1\s*</address>"; then
        echo "Error: Address $link_address_v6_1 not found (FAIL)."
        cleanup
        exit 1
    fi

    echo "Interface $1 configuration verified successfully (PASS)."
}

# Function to check interface VLAN in sysrepo
check_vlan() {
    echo "- Checking vlan ($1) configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-ip-link:links/vlan[name=\"$1\"]")
    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)."
        cleanup
        exit 1
    fi

    echo "Checking name field"
    if ! echo "$output" | grep -qP "<name>\s*$1\s*</name>"; then
        echo "Error: VLAN Interface Name $1 not found (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking mtu field"
    if ! echo "$output" | grep -qP "<mtu>\s*$mtu_value\s*</mtu>"; then
        echo "Error: MTU value $mtu_value for $1 not found (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking VLAN ID field"
    if ! echo "$output" | grep -qP "<id>\s*$vlan_id\s*</id>"; then
        echo "Error: VLAN ID $vlan_id for $1 not found (FAIL)."
        cleanup
        exit 1
    fi

    echo "VLAN $1 configuration verified successfully (PASS)."
}

# Function to check interface VLAN in sysrepo
check_vti() {
    echo "- Checking vti ($1) configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-ip-link:links/vti[name=\"$1\"]")
    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)"
        delete_interfaces
        exit 1
    fi

    echo "Checking name field"
    if ! echo "$output" | grep -qP "<name>\s*$1\s*</name>"; then
        echo "Error: VTI interface Name $1 not found (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "Checking mtu field"
    if ! echo "$output" | grep -qP "<mtu>\s*$mtu_value\s*</mtu>"; then
        echo "Error: MTU value $mtu_value for $1 not found for vti interface $1 (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "Checking local address field"
    if ! echo "$output" | grep -qP "<local>\s*$vti_local\s*</local>"; then
        echo "Error: Local address $vti_local not found for vti interface $1 (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "Checking remote address field"
    if ! echo "$output" | grep -qP "<remote>\s*$vti_remote\s*</remote>"; then
        echo "Error: Local address $vti_remote not found for vti interface $1 (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "VTI Interface $1 configuration verified successfully (PASS)."
}

echo -e "\nVLIDATING DATA ON SYSREPO"
check_interface $link_name
echo -e "\n"
check_vlan $vlan_name
echo -e "\n"
check_vti $vti_name
echo -e "\n"
cleanup

# Final check for errors
if [ $ret -ne 0 ]; then
    echo "TEST-ERROR: One or more test scripts failed. (FAIL)"
    exit $ret
else
    echo "TEST-INFO: All Tests Completed Successfully (PASS)"
fi

# Exit script with the final return value
exit $ret
