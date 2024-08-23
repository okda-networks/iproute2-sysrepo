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

vxlan_name="oper_vxlan"
vxlan_id="1000"
vxlan_local="10.20.30.40"
vxlan_remote="10.120.130.140"
vxlan_dstport="4789"

route_if1="route1"
route_if1_address="11.11.11.11/24"
route_if1_nh_address="11.11.11.11"
route_if2="route2"
route_if2_address="22.22.22.22/16"
route_pref1="111.111.111.0/24"
route_pref2="222.222.222.222/31"
route_table="100"
route_metric="8"
route_tos="EF"

qdisc_if1="qdisc_if1"
qdisc_if2="qdisc_if2"
qdisc_if3="qdisc_if3"
qdisc_if4="qdisc_if4"
qdisc_if5="qdisc_if5"
qdisc_if6="qdisc_if6"
qdisc_if7="qdisc_if7"
qdisc_if8="qdisc_if8"
qdisc_if9="qdisc_if9"

# Function to create LINK
create_interfaces() {
    ip link $operation name $1 mtu $2 type $3
    echo "ip link $operation name $1 mtu $2 type $3"
}

# Function to set link ip address
add_dev_address() {
    ip address add dev $1 $2
    echo "ip address add dev $1 $2"
}

# Function to create VLAN
create_vlan() {
    ip link $operation name $1 mtu $2 link $3 type vlan protocol 802.1q id $4
    echo "ip link $operation name $1 mtu $2 link $3 type vlan protocol 802.1q id $4"
}

# Function to create VTI
create_vti() {
    ip link $operation name $1 mtu $2 type vti local $3 remote $4
    echo "ip link $operation name $1 mtu $2 type vti local $3 remote $4"
}

# Function to create VXLAN
create_vxlan() {
    ip link $operation name $1 mtu $2 type vxlan id $3 remote $4 local $5 dstport $6 l3miss
    echo "ip link $operation name $1 mtu $2 type vxlan id $3 remote $4 local $5 dstport $6 l3miss"
}

# Function to set interface status (UP/DOWN)
set_interface_status() {
    ip link set name $1 $2
    echo "ip link set name $1 $2"
}


# Function to create simple route
create_simple_route() {
    ip route add $1 dev $2
    echo "ip route add $1 dev $2"
}

# Function to create advanced route
create_advanced_route() {
    ip route add $1 table $2 metric $3 tos $4 nexthop via $5 weight $6 nexthop dev $7 weight $8
    echo "ip route add $1 table $2 metric $3 tos $4 nexthop via $5 weight $6 nexthop dev $7 weight $8"
}

# Function to delete network interfaces
delete_interfaces() {
    ip link del name $vlan_name
    ip link del name $link_name
    ip link del name $vti_name
    ip link del name $vxlan_name

    ip link del name $route_if1
    ip link del name $route_if2

    ip link del name $qdisc_if1
    ip link del name $qdisc_if2
    ip link del name $qdisc_if3
    ip link del name $qdisc_if4
    ip link del name $qdisc_if5
    ip link del name $qdisc_if6
    ip link del name $qdisc_if7
    ip link del name $qdisc_if8
    ip link del name $qdisc_if9
}

# Function to delete network interfaces
delete_routes() {
    ip route del $route_pref1
    ip route del $route_pref2 table $route_table metric $route_metric tos $route_tos
}


echo "#############################################"
echo "## Running system startup config load test ##"
echo "#############################################"

echo -e "\nCREATING TEST SYSTEM CONFIG ON LINUX"
# Create dummy, VLAN, and VTI interfaces with specified attributes
create_interfaces $link_name $mtu_value "dummy"
add_dev_address $link_name $link_address_v4_1
add_dev_address $link_name $link_address_v6_1
add_dev_address $link_name $link_address_v4_2
create_vlan $vlan_name $mtu_value $link_name $vlan_id
create_vti $vti_name $mtu_value $vti_local $vti_remote
create_vxlan $vxlan_name $mtu_value $vxlan_id $vxlan_remote $vxlan_local $vxlan_dstport

# Route interfaces
create_interfaces $route_if1 $mtu_value "dummy"
create_interfaces $route_if2 $mtu_value "dummy"
add_dev_address $route_if1 $route_if1_address
add_dev_address $route_if2 $route_if2_address
set_interface_status $route_if1 "up"
set_interface_status $route_if2 "up"

# Create Routes
create_simple_route $route_pref1 $route_if1
create_advanced_route $route_pref2 $route_table $route_metric $route_tos $route_if1_nh_address "5" $route_if2 "6"

# QDISC
create_interfaces $qdisc_if1 $mtu_value "dummy"
create_interfaces $qdisc_if2 $mtu_value "dummy"
create_interfaces $qdisc_if3 $mtu_value "dummy"
create_interfaces $qdisc_if4 $mtu_value "dummy"
create_interfaces $qdisc_if5 $mtu_value "dummy"
create_interfaces $qdisc_if6 $mtu_value "dummy"
create_interfaces $qdisc_if7 $mtu_value "dummy"
create_interfaces $qdisc_if8 $mtu_value "dummy"
create_interfaces $qdisc_if9 $mtu_value "dummy"

#prio
tc qdisc add dev $qdisc_if1 root handle 1: prio bands 3 priomap 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2 2
#sfq
tc qdisc add dev $qdisc_if1 parent 1:1 handle 10: sfq
#tbf
tc qdisc add dev $qdisc_if1 parent 1:2 handle 20: tbf rate 20kbit buffer 1600 limit 3000
#codel
tc qdisc add dev $qdisc_if2 root handle 1: codel limit 1000 interval 100ms target 5ms ecn ce_threshold 20
#fq_codel
tc qdisc add dev $qdisc_if3 root handle 1: fq_codel target 5ms interval 100ms limit 1124 flows 1124 quantum 300 memory_limit 50 ecn ce_threshold 64ms
# bfifo
tc qdisc add dev $qdisc_if4 root bfifo
# pfifo_fast
tc qdisc add dev $qdisc_if5 root handle 20: pfifo_fast
# pfifo
tc qdisc add dev $qdisc_if6 root handle 20: pfifo
#ingress
tc qdisc add dev $qdisc_if7 ingress_block 10 ingress
#clsact
tc qdisc add dev $qdisc_if8 ingress_block 11 egress_block 12 clsact

# TC-FILTERs
# shared-block-filter
tc filter add block 11 pref 10 protocol ip flower dst_ip 10.10.10.0/24 action drop
tc filter add block 11 pref 20 protocol ip flower src_ip 11.11.11.11 action nat ingress 11.11.11.11 100.100.100.100
tc filter add block 12 pref 10 protocol ip flower src_ip 11.11.0.0/16 action pass
tc filter add block 12 pref 20 protocol ip flower ip_proto tcp src_port 50 action drop
tc filter add block 12 pref 30 protocol ip flower ip_proto udp dst_port 50 action drop
tc filter add block 12 pref 40 protocol ip flower src_mac 11:11:11:11:11:11 action drop


# dev-filter
tc qdisc add dev $qdisc_if9 clsact
tc filter add dev $qdisc_if9 ingress pref 10 protocol ip flower dst_ip "10.10.10.10" action drop
tc filter add dev $qdisc_if9 ingress pref 20 protocol ip flower ip_proto tcp src_port 50 action drop
tc filter add dev $qdisc_if9 ingress pref 30 protocol ip flower src_ip "12.12.12.12" action nat ingress "12.12.12.12/32" "100.100.100.100"
tc filter add dev $qdisc_if9 egress pref 10 protocol ip flower dst_ip "13.13.13.13" action pass

# qdisc-filter
# not-supported

# Run iproute2-sysrepo and store its PID
echo -e "\nSTARTING IPROUTE2-SYSREPO"
./bin/iproute2-sysrepo --no-monitor 2>&1 &
sysrepo_pid=$!
sleep 0.5

# Function to cleanup
cleanup() {
  delete_routes
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

# Function to check interface VTI in sysrepo
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

# Function to check interface VXLAN in sysrepo
check_vxlan() {
    echo "- Checking vxlan ($1) configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-ip-link:links/vxlan[name=\"$1\"]")
    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)"
        delete_interfaces
        exit 1
    fi

    echo "Checking name field"
    if ! echo "$output" | grep -qP "<name>\s*$1\s*</name>"; then
        echo "Error: VXLAN interface Name $1 not found (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "Checking mtu field"
    if ! echo "$output" | grep -qP "<mtu>\s*$mtu_value\s*</mtu>"; then
        echo "Error: MTU value $mtu_value for $1 not found for VXLAN interface $1 (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "Checking local address field"
    if ! echo "$output" | grep -qP "<local>\s*$vxlan_local\s*</local>"; then
        echo "Error: Local address $vxlan_local not found for VXLAN interface $1 (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "Checking remote address field"
    if ! echo "$output" | grep -qP "<remote>\s*$vxlan_remote\s*</remote>"; then
        echo "Error: Local address $vxlan_remote not found for VXLAN interface $1 (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "Checking dstport field"
    if ! echo "$output" | grep -qP "<dstport>\s*$vxlan_dstport\s*</dstport>"; then
        echo "Error: VXLAN dstport $vxlan_dstport not found for VXLAN interface $1 (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "Checking l3miss field"
    if ! echo "$output" | grep -qP "<l3miss>true</l3miss>"; then
        echo "Error: VXLAN l3miss flag not found for VXLAN interface $1 (FAIL)."
        delete_interfaces
        exit 1
    fi

    echo "VXLAN Interface $1 configuration verified successfully (PASS)."
}

# Function to check simple route in sysrepo
check_simple_route() {
    echo "- Checking route ($1) configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-ip-route:routes/route[prefix=\"$1\"]")
    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)."
        cleanup
        exit 1
    fi

    echo "Checking prefix field"
    if ! echo "$output" | grep -qP "<prefix>\s*$1\s*</prefix>"; then
        echo "Error: Route Prefix $1 not found (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking dev field"
    if ! echo "$output" | grep -qP "<dev>\s*$2\s*</dev>"; then
        echo "Error: nexthop dev $2 for $1 not found (FAIL)."
        cleanup
        exit 1
    fi
}

# Function to check advanced route in sysrepo
check_advanced_route() {
    echo "- Checking route ($1) configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-ip-route:routes/route[prefix=\"$1\"]")
    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)."
        cleanup
        exit 1
    fi

    echo "Checking prefix field"
    if ! echo "$output" | grep -qP "<prefix>\s*$1\s*</prefix>"; then
        echo "Error: Route Prefix $1 not found (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking table field"
    if ! echo "$output" | grep -qP "<table>\s*$2\s*</table>"; then
        echo "Error: Route table $2 not found for prefix $1 (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking metric field"
    if ! echo "$output" | grep -qP "<metric>\s*$3\s*</metric>"; then
        echo "Error: Route metric $3 not found for prefix $1 (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking TOS field"
    if ! echo "$output" | grep -qP "<tos>\s*$4\s*</tos>"; then
        echo "Error: Route TOS $4 not found for prefix $1 (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking 'via address' route nexthop field"
    if ! echo "$output" | grep -qP "<address>\s*$5\s*</address>"; then
        echo "Error: nexthop address $5 not found for prefix $1 (FAIL)."
        cleanup
        exit 1
    fi

    echo "Checking 'via dev' route nexthop field"
    if ! echo "$output" | grep -qP "<dev>\s*$6\s*</dev>"; then
        echo "Error: nexthop address $6 not found for prefix $1 (FAIL)."
        cleanup
        exit 1
    fi
}

# Function to check generic qidscs sysrepo
check_generic_qdiscs() {
    echo "- Checking interface ($1) qdisc configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-tc-qdisc:qdiscs/qdisc[dev=\"$1\"]")

    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)."
        cleanup
        exit 1
    fi

    echo "Checking qdisc kind field"
    if ! echo "$output" | grep -qP "<qdisc-kind>\s*$2\s*</qdisc-kind>"; then
        echo "Error: qdisc kind $2 not found for interface $1 (FAIL)."
        cleanup
        exit 1
    fi
}

# Function to check special qdiscs sysrepo
check_special_qdiscs() {
    echo "- Checking interface ($1) special qdisc configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-tc-qdisc:qdiscs/special-qdisc[dev=\"$1\"]")

    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)."
        cleanup
        exit 1
    fi

    echo "Checking special qdisc kind field"
    if ! echo "$output" | grep -qP "<qdisc-kind>\s*$2\s*</qdisc-kind>"; then
        echo "Error: special qdisc kind $2 not found for interface $1 (FAIL)."
        cleanup
        exit 1
    fi
    echo "Checking special qdisc ingress_block field"
    if ! echo "$output" | grep -qP "<ingress_block>\s*$3\s*</ingress_block>"; then
        echo "Error: special qdisc ingress_block $3 not found for interface $1 qdisc kind $2 (FAIL)."
        cleanup
        exit 1
    fi
}

# Function to check shared block filter rules
check_shared_block_rule() {
    echo "- Checking shared block ($1) filter rule ($2) configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-tc-filter:tc-filters/shared-block-filter[block=\"$1\"]/rule[pref=\"$2\"]")

    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)."
        cleanup
        exit 1
    fi

    local i=3
    local args=("$@")
    while [ $i -lt $# ]; do
        echo "Checking rule condition or action (${args[$((i-1))]}):"
        pattern="<${args[$((i-1))]}>\\s*${args[$i]}\\s*</${args[$((i-1))]}>"
        
        # Use the pattern with grep
        if ! echo "$output" | grep -qP "$pattern"; then
            echo "Error: rule condition (${args[$((i-1))]}) not found or not matching expected value of (${args[$i]})"
            cleanup
            exit 1
        fi
        echo "    found expected value (${args[$i]})"
        i=$((i+2))
    done
}

# Function to check dev-filter rules
check_dev_filter_rule() {
    echo "- Checking dev ($1) filter direction ($2) filter rule ($3) configuration on sysrepo:"
    output=$(sysrepocfg -X -d running -f xml -x "/iproute2-tc-filter:tc-filters/dev-filter[dev=\"$1\"][direction=\"$2\"]/rule[pref=\"$3\"]")

    echo -e "sysrepo outputs:\n $output"

    if [ $? -ne 0 ]; then
        echo "Error: sysrepo failed to extract data for $1 (SYSREPO PROBLEM ?)."
        cleanup
        exit 1
    fi

    local i=4
    local args=("$@")
    while [ $i -lt $# ]; do
        echo "Checking rule condition or action (${args[$((i-1))]}):"
        pattern="<${args[$((i-1))]}>\\s*${args[$i]}\\s*</${args[$((i-1))]}>"
        
        # Use the pattern with grep
        if ! echo "$output" | grep -qP "$pattern"; then
            echo "Error: rule condition (${args[$((i-1))]}) not found or not matching expected value of (${args[$i]})"
            cleanup
            exit 1
        fi
        echo "    found expected value (${args[$i]})"
        i=$((i+2))
    done
}

echo -e "\nVLIDATING DATA ON SYSREPO"
check_interface $link_name
echo -e "\n"
check_vlan $vlan_name
echo -e "\n"
check_vti $vti_name
echo -e "\n"
check_vxlan $vxlan_name
echo -e "\n"
check_simple_route $route_pref1 $route_if1
echo -e "\n"
check_advanced_route $route_pref2 $route_table $route_metric $route_tos $route_if1_nh_address $route_if2
echo -e "\n"
check_generic_qdiscs $qdisc_if1 "prio"
echo -e "\n"
check_generic_qdiscs $qdisc_if1 "sfq"
echo -e "\n"
check_generic_qdiscs $qdisc_if1 "tbf"
echo -e "\n"
check_generic_qdiscs $qdisc_if2 "codel"
echo -e "\n"
check_generic_qdiscs $qdisc_if3 "fq_codel"
echo -e "\n"
check_generic_qdiscs $qdisc_if4 "bfifo"
echo -e "\n"
check_generic_qdiscs $qdisc_if5 "pfifo_fast"
echo -e "\n"
check_generic_qdiscs $qdisc_if6 "pfifo"
echo -e "\n"
check_special_qdiscs $qdisc_if7 "ingress" "10"
echo -e "\n"
check_special_qdiscs $qdisc_if8 "clsact" "11"
echo -e "\n"
check_shared_block_rule "11" "10" "dst_ip" "10.10.10.0/24" "gact" "drop"
echo -e "\n"
check_shared_block_rule "11" "20" "src_ip" "11.11.11.11" "direction" "ingress" "old_ip" "11.11.11.11/32" "new_ip" "100.100.100.100" "gact" "pass"
echo -e "\n"
check_shared_block_rule "12" "10" "src_ip" "11.11.0.0/16" "gact" "pass"
echo -e "\n"
check_shared_block_rule "12" "20" "ip_proto" "tcp" "src_port" "50" "gact" "drop"
echo -e "\n"
check_shared_block_rule "12" "30" "ip_proto" "udp" "dst_port" "50" "gact" "drop"
echo -e "\n"
check_shared_block_rule "12" "40" "src_mac" "11:11:11:11:11:11" "gact" "drop"
echo -e "\n"
check_dev_filter_rule $qdisc_if9 "ingress" "10" "dst_ip" "10.10.10.10" "gact" "drop"
echo -e "\n"
check_dev_filter_rule $qdisc_if9 "ingress" "20" "ip_proto" "tcp" "src_port" "50" "gact" "drop"
echo -e "\n"
check_dev_filter_rule $qdisc_if9 "ingress" "30" "src_ip" "12.12.12.12" "direction" "ingress" "old_ip" "12.12.12.12/32" "new_ip" "100.100.100.100" "gact" "pass"
echo -e "\n"
check_dev_filter_rule $qdisc_if9 "egress" "10" "dst_ip" "13.13.13.13" "gact" "pass"
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
