# yp_sr
Yang iproute2 PoC - do not use it yet

# Build

```
$ apt install libelf-dev libbpf-dev libbsd-dev libsysrepo-dev libcap-dev libmnl-dev libselinux-dev
$ git clone --recursive https://github.com/vjardin/yp_sr.git
$ make -j $(nproc)
$ bin/yp_sr
```
