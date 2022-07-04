# Running lwIP on DPDK

This repository contains an extremely lightweight web server application, built atop lwIP and DPDK.

The primary objective of this repository is to run a quick benchmark to see the baseline performance of lwIP running on DPDK.

We assume to use [wrk](https://github.com/wg/wrk) as the HTTP benchmark client, to measure the throughput and latency.

While we use the HTTP benchmark client, this workload would be simple TCP ping-pong.

## Target platform

This repository assumes Linux on x86-64 CPUs.

The author tested this on Ubuntu 20.04.

## Requirements

meson and ninja are used for DPDK compilation. ( Please see https://doc.dpdk.org/guides/linux_gsg/build_dpdk.html for details. )

Please install them if you do not have yet.

- meson
- ninja

## Build

The following commands will compile the lwIP and DPDK applied application.

```
git clone https://github.com/yasukata/tinyhttpd-lwip-dpdk.git
```

```
cd tinyhttpd-lwip-dpdk
```

```
make
```

### What Makefile does

Makefile produces a file named app that is bound with lwIP and DPDK.

Our Makefile downloads the source code of lwIP and DPDK, and compile them. The source code and the build objects of lwIP and DPDK are located in the directory where our Makefile is located.

The following is the detailed procedure that our Makefile conducts.

1. download required the source code of lwIP and DPDK.

- lwIP: ([LOCATION_OF_MAKEFILE]/lwip/lwip-$(LWIP_VER).zip) http://download.savannah.nongnu.org/releases/lwip/lwip-$(LWIP_VER).zip
- lwIP contrib: ([LOCATION_OF_MAKEFILE]/lwip/contrib-$(CONTRIB_VER).zip) http://download.savannah.nongnu.org/releases/lwip/contrib-$(CONTRIB_VER).zip
- DPDK: ([LOCATION_OF_MAKEFILE]/dpdk/dpdk-$(DPDK_VER).tar.xz) https://fast.dpdk.org/rel/dpdk-$(DPDK_VER).tar.xz

2. extract the source code

- lwIP: ([LOCATION_OF_MAKEFILE]/lwip/lwip-$(LWIP_VER))
- lwIP contrib: ([LOCATION_OF_MAKEFILE]/lwip/contrib-$(CONTRIB_VER))
- DPDK: ([LOCATION_OF_MAKEFILE]/dpdk/dpdk-$(DPDK_VER))

3. compile and install DPDK

DPDK is installed in a directory [LOCATION_OF_MAKEFILE]/dpdk/install .

Therefore, you do not need the root permission for installation, and this does not overwrite the existing DPDK library.

## How to use

We assume the command is executed in the top directory of this repository. So, please cd to tinyhttpd-lwip-dpdk.

```
cd tinyhttpd-lwip-dpdk
```

### Launch app with a tap device

The following command launches the application. This example uses a tap device for the network interface so that users, who do not have an extra network interface for DPDK, also can try.

**WARNING: the following command is executed with sudo. so, please conduct the following procedure only when you understand what you are doing.**

Here, the root permission is necessary to execute DPDK.

```
sudo LD_LIBRARY_PATH=./dpdk/install/lib/x86_64-linux-gnu ./app -l 0-1 --proc-type=primary --file-prefix=pmd1 --vdev=net_tap001,iface=tap001 --no-pci -- -a 10.0.0.2 -g 10.0.0.1 -m 255.255.255.0 -l 1 -p 10000
```

In the command above, ```app [option for DPDK] -- [option for lwIP and app]```

Option for DPDK:
- ```--vdev=net_tap001,iface=tap001```: the network interface to be used

Please refer to https://doc.dpdk.org/guides/nics/tap.html for details.

Option for lwIP and app:
- ```-a```: IP address (10.0.0.2)
- ```-g```: gateway address (10.0.0.1)
- ```-m```: netmask (255.255.255.0)
- ```-l```: content length that the web server app serves (1)
- ```-p```: the port the web server app listens on (10000)

### It didn't work?

If the app does not work, and if particularly failing at rte_eal_init, it would be due to lack of hugepages.

Please type the following command to see how many hugepages are available on your machine.

```
cat /proc/sys/vm/nr_hugepages
```

If the output is 0, it means your machine has no hugepage.

If you wish to request the OS to have some hugepages, you can do that by the following command; the example command below requests to preserve 64 MB of hugepages, and please change the hugepage size according to your machine's DRAM size.

NOTE: if you are not familiar with hugepages, it would be recommended to begin by giving **sufficiantly small DRAM for hugepages**, compared to the entire DRAM that your machine has.

**WARNING: the following command requires sudo. please type the following command only when you understand what you are doing.**

```
sudo ./dpdk/dpdk-22.03/usertools/dpdk-hugepages.py -p 2M -r 64M
```

After running the command above, if you try the same following command to see the configured hugepages, it will say 32; it means 32 of 2 MB hugepages (equal 64 MB).

```
cat /proc/sys/vm/nr_hugepages
```

You can find a detailed explanation in https://doc.dpdk.org/guides/linux_gsg/sys_reqs.html .

### Configure a bridge for the tap device

Once you launch the app by the command above, DPDK polls the tap device named tap001, and lwIP waits for the packets for 10.0.0.2.

In this example, we send packets to tap001 through a bridge.

The following command does:
- create a bridge named br001
- configure the bridge address as 10.0.0.1/24
- turn on the bridge
- finally, attache tap001 to br001

```
sudo brctl addbr br001; sudo ifconfig br001 10.0.0.1 netmask 255.255.255.0 up; sudo brctl addif br001 tap001
```

After you do this, please try ping first to confirm the reachability to lwIP. Supposedly, you fill receive pong from lwIP (10.0.0.2).

```
ping 10.0.0.2
```

### Benchmark client

For the benchmark client, we recommend to use [wrk](https://github.com/wg/wrk).

Please visit https://github.com/wg/wrk for details.

### Launch app with a physical network interface

If you have an extra physial network interface, you can test this application with it.

**WARNING: you may lose the reachability to your machine if you make a wrong configuration. so, please conduct the following procedure only when you understand what you are doing.**

The following command shows the currently availabe network interfaces.

```
./dpdk/dpdk-22.03/usertools/dpdk-devbind.py -s
```

You will see like as follows. ( if you do not see this kind of output, it may mean that your machine does not have the interfaces that can be attached to DPDK. )

```
Network devices using kernel driver
===================================
0000:04:00.0 '82599ES 10-Gigabit SFI/SFP+ Network Connection 10fb' if=ens3f0 drv=ixgbe unused=vfio-pci
0000:04:00.1 '82599ES 10-Gigabit SFI/SFP+ Network Connection 10fb' if=ens3f1 drv=ixgbe unused=vfio-pci
```

Let's say, we wish to use the 10 Gbps network interface identified by 0000:04:00.1. ( please select according to your environment. )

The output above says that 0000:04:00.1 is currently managed by the kernel-space ixgbe driver.

To use it with DPDK, we need to bind it with the vfio-pci driver, and the following command does it.

```
sudo ./dpdk/dpdk-22.03/usertools/dpdk-devbind.py -b vfio-pci 0000:04:00.1
```

Afterward, you will see the following if you do ```dpdk-devbind.py -s``` above again; this means that 0000:04:00.1 is bound with vfio-pci.

```
Network devices using DPDK-compatible driver
============================================
0000:04:00.1 '82599ES 10-Gigabit SFI/SFP+ Network Connection 10fb' drv=vfio-pci unused=ixgbe
```

Then, the following command launches the application using the 0000:04:00.1 network interface.

```
sudo LD_LIBRARY_PATH=./dpdk/install/lib/x86_64-linux-gnu ./app -l 0-1 --proc-type=primary --file-prefix=pmd1 --allow=0000:04:00.1 -- -a 10.0.0.2 -g 10.0.0.1 -m 255.255.255.0 -l 1 -p 10000
```

## Rough numbers

Here, we provide rough numbers and comparison with the default Linux TCP stack.

### Comparison with Linux

For the Linux TCP stack test, we use the following C program, that is conceptionally equivalent to our lwIP and DPDK applied application.

```
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <assert.h>

#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/epoll.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

int main(int argc, char* const* argv)
{
	int ch;
	int port = 10000;
	size_t content_len = 1, httpdatalen;
	int epfd, sockfd;
	int on;
	char *httpbuf = NULL;

	while ((ch = getopt(argc, argv, "p:l:")) != -1) {
		switch (ch) {
		case 'l':
			content_len = atoi(optarg);
			break;
		case 'p':
			port = atoi(optarg);
			break;
		default:
			assert(0);
			break;
		}
	}

	{
		size_t buflen = content_len + 256 /* for http hdr */;
		char *content;
		assert((httpbuf = (char *) malloc(buflen)) != NULL);
		assert((content = (char *) malloc(content_len + 1)) != NULL);
		memset(content, 'A', content_len);
		content[content_len] = '\0';
		httpdatalen = snprintf(httpbuf, buflen, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\nConnection: keep-alive\r\n\r\n%s",
				content_len, content);
		free(content);
		printf("http data length: %lu bytes\n", httpdatalen);
	}

	assert((epfd = epoll_create1(EPOLL_CLOEXEC)) != -1);
	assert((sockfd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) != -1);
	on = 1;
	assert(!setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)));
	on = 1;
	assert(!setsockopt(sockfd, SOL_TCP, TCP_NODELAY, &on, sizeof(on)));
	on = 1;
	assert(!ioctl(sockfd, FIONBIO, &on));
	{
		struct sockaddr_in sin = {
			.sin_family = AF_INET,
			.sin_addr.s_addr = htonl(INADDR_ANY),
			.sin_port = htons(port),
		};
		assert(!bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)));
	}
	assert(!listen(sockfd, SOMAXCONN));
	{
		struct epoll_event ev = {
			.events = EPOLLIN,
			.data.fd = sockfd,
		};
		assert(!epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev));
	}

	while (1) {
		struct epoll_event evts[256];
		int i, nfd = epoll_wait(epfd, evts, 256, -1);
		for (i = 0; i < nfd; i++) {
			if (evts[i].data.fd == sockfd) {
				struct sockaddr_in caddr_in;
				socklen_t addrlen;
				int newfd;
				while ((newfd = accept(evts[i].data.fd, (struct sockaddr *)&caddr_in, &addrlen)) != -1) {
					struct epoll_event ev = {
						.events = EPOLLIN,
						.data.fd = newfd,
					};
					assert(!epoll_ctl(epfd, EPOLL_CTL_ADD, newfd, &ev));
				}
			} else {
				ssize_t len;
				static char buf[70000];
				len = read(evts[i].data.fd, buf, sizeof(buf));
				if (len == 0) {
					close(evts[i].data.fd);
					continue;
				} else if (len < 0)
					continue;
				if (strncmp(buf, "GET ", strlen("GET ")) == 0)
					assert(httpdatalen == write(evts[i].data.fd, httpbuf, httpdatalen));
			}
		}
	}

	free(httpbuf);
	
	return 0;
}
```

### Workload

The server applications serve the static content, and the entier HTTP message, including the HTTP header, is 64 bytes.

We change the number of concurrent client connections of wrk from 1 to 128.

```
$ ./wrk http://10.0.0.2:10000/ -d 10 -t 1 -c 1 -L
$ ./wrk http://10.0.0.2:10000/ -d 10 -t 2 -c 2 -L
$ ./wrk http://10.0.0.2:10000/ -d 10 -t 4 -c 4 -L
$ ./wrk http://10.0.0.2:10000/ -d 10 -t 8 -c 8 -L
$ ./wrk http://10.0.0.2:10000/ -d 10 -t 16 -c 16 -L
$ ./wrk http://10.0.0.2:10000/ -d 10 -t 16 -c 32 -L
$ ./wrk http://10.0.0.2:10000/ -d 10 -t 16 -c 64 -L
$ ./wrk http://10.0.0.2:10000/ -d 10 -t 16 -c 128 -L
```

### Hardware

Using two machines, and each has:
- CPU: 2 x 8-core Intel Xeon CPU E5-2640 v3 @ 2.60GHz
- NIC: Intel 82599ES 10-Gigabit

The two machines are directly connected via the 10 Gbps NICs.

### Result

lwIP on DPDK achieves 51.7~483.5% higher throughput compared to the Linux TCP stack.

Throughput ( Requests/sec )

| Concurrent connections | Linux TCP | lwIP on DPDK |
| ------------- | ------------- | ------------- |
|   1 |  19688.15 |   29871.01 |
|   2 |  36760.34 |   60116.91 |
|   4 |  39310.89 |  128728.74 |
|   8 |  74619.08 |  253518.76 |
|  16 | 123529.06 |  457784.18 |
|  32 | 187544.78 |  795987.05 |
|  64 | 189746.58 | 1107349.60 |
| 128 | 189733.81 |  870747.60 |

