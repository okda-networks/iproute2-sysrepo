# Overview
IPRoute2-Sysrepo is module wrapped around IPRout2 to allow yang based configuration and operational data pulling.  
The module integrates iproute2 with sysrepo as a plugin and uses sysrepo data store for configuring iproute2 submodules.  

With this enactment IPRoute2-Sysrepo adds the following features to IPRoute2:  
- Configuration persistence using sysrepo data stores.
- Transactional configuration via NETCONF (Netopeer2) and ONM-CLI and other yang compliant configuration tools.
- Operational data load and streaming.

IPRoute2-sysrepo code is designed as a wrapper around the original IPRoute2 code, with very minimal changes, ensuring compatibility with all versions of IPRoute2. It is committed to staying current with the newest releases of IPRoute2.  
  
More details about its architecture can be found in the [documentation page](https://docs.okdanetworks.com/onm/IPRoute2-Sysrepo/overview).  

# Supported Features:
- ip link
- ip netns
- ip address
- ip nexthop
- ip route
- ip rule
- ip neighbor
- tc qdisc
- tc filter
- tc class


# Building

## Requirements:

**- Required packages installation:**
```
sudo apt update
sudo apt install libelf-dev libbpf-dev libbsd-dev libcap-dev libmnl-dev libselinux-dev libjson-c-dev bison flex git cmake build-essential libpcre2-dev
```

**- Libyang:**
```
git clone https://github.com/CESNET/libyang.git
cd libyang/
git checkout v2.1.148
mkdir build; cd build
cmake -D CMAKE_BUILD_TYPE:String="Release" ..
make
sudo make install
```

**- Sysrepo:**
```
git clone https://github.com/sysrepo/sysrepo.git
cd sysrepo
git checkout v2.2.150
mkdir build; cd build
cmake -DCMAKE_BUILD_TYPE:String="Release" ..
make
sudo make install
ldconfig
``` 

**- JSON-C**
```
git clone https://github.com/json-c/json-c.git
mkdir json-c-build
cd json-c-build
cmake ../json-c
make
make test
sudo make install
ldconfig
```

## Build IPRoute2-Sysrepo:
```
git clone --recursive https://github.com/okda-networks/iproute2-sysrepo.git
cd iproute2-sysrepo
make
sudo make install
```

# Installation

After successful installation, you will need to load IPRoute2-Sysrepo yang modules to sysrepo:
```
cd yang/
sysrepoctl -i iproute2-cmdgen-extensions.yang
sysrepoctl -i okda-onmcli-extensions.yang
sysrepoctl -i ietf-inet-types.yang
sysrepoctl -i ietf-yang-types.yang
sysrepoctl -i iproute2-ip-netns.yang
sysrepoctl -i iproute2-ip-link.yang
sysrepoctl -i iproute2-ip-nexthop.yang
sysrepoctl -i iproute2-net-types.yang
sysrepoctl -i iproute2-ip-route.yang
sysrepoctl -i iproute2-tc-qdisc.yang
sysrepoctl -i iproute2-tc-filter.yang
sysrepoctl -i iproute2-ip-rule.yang
sysrepoctl -i iproute2-ip-neighbor.yang
```

# Starting IPRoute2-Sysrepo
With IPRoute2-Sysrepo you can configure your linux networking using the following methods:  
- Netconf: you will need to install [Netopeer2 server](https://github.com/CESNET/netopeer2) and use any netconf client to edit the data of iproute2 yang modules.
- [ONM-CLI](https://github.com/okda-networks/onm-cli): it allows you to configure sysrepo loaded yang modules in a user-friendly cli without the need for netconf.
- sysrepocfg tool: this is a test tool from sysrepo to edit and view sysrepo data stores configuration.

IPRoute2-sysrepo needs to be running while using the tools above:
```
$ sudo iproute2-sysrepo
```

# Testing
Below will be demonstrating creating the following interfaces in one transaction using sysrepocfg tool:  
- veth interface with ipv4 and ipv6 addresses.
- vlan interface with id 10.
- veth interface mapped to vlan 10.
- bridge interface.

1- Start IPRoute2-Sysrepo and keep it running:
```
$ sudo iproute2-sysrepo
```

2- In a new terminal window copy the following xml data into a new .xml file:  
  
**test_links.xml**
```
<links xmlns="urn:okda:iproute2:ip:link">
    <link>
        <name>testIf0</name>
        <type>veth</type>
        <master>br1</master>
        <ip>
            <address>192.0.2.1/32</address>
        </ip>
        <ip>
            <address>2001:bd8::11/64</address>
        </ip>
    </link>
    <link>
        <name>testIf1</name>
        <type>veth</type>
    </link>
    <vlan>
        <name>vlan10</name>
        <device>testIf1</device>
        <vlan-info>
            <protocol>802.1q</protocol>
            <id>10</id>
        </vlan-info>
    </vlan>
    <bridge>
        <name>br1</name>
        <br-info>
            <ageing_time>1000</ageing_time>
        </br-info>
    </bridge>
</links>
```

3- use the following command to apply the configuration to sysrepo running data store:
```
sysrepocfg --edit test_links.xml -d running
```

4- Interfaces should be created automatically on your linux  
You can use legacy iproute2 commands to view config:
```
ip link show
ip address show
```

Or get the config from sysrepo operational data store:
```
sysrepocfg -X -d operational -f json  -x "/iproute2-ip-link:links"
```
