# IPRoute2-Sysrepo Module
Yang iproute2 PoC - do not use it yet

# Build

```
$ apt install libelf-dev libbpf-dev libbsd-dev libsysrepo-dev libcap-dev libmnl-dev libselinux-dev
$ git clone --recursive https://github.com/okda-networks/iproute2-sysrepo.git
$ make -j $(nproc)
$ bin/iproute2-sysrepo
```
