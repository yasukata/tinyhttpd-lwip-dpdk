/*
 *
 * Copyright 2022 Kenichi Yasukata
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include <arpa/inet.h>

#include <rte_common.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memcpy.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_atomic.h>
#include <rte_cycles.h>
#include <rte_prefetch.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_branch_prediction.h>
#include <rte_interrupts.h>
#include <rte_random.h>
#include <rte_debug.h>
#include <rte_ether.h>
#include <rte_ethdev.h>
#include <rte_mempool.h>
#include <rte_mbuf.h>
#include <rte_bus_pci.h>

/* workaround to avoid conflicts between dpdk and lwip definitions */
#undef IP_DF
#undef IP_MF
#undef IP_RF
#undef IP_OFFMASK

#include <lwip/opt.h>
#include <lwip/init.h>
#include <lwip/pbuf.h>
#include <lwip/netif.h>
#include <lwip/etharp.h>
#include <lwip/tcpip.h>
#include <lwip/tcp.h>
#include <lwip/timeouts.h>
#include <lwip/prot/tcp.h>

#include <netif/ethernet.h>

#define MAX_PKT_BURST (32)
#define NUM_SLOT (256)

#define MEMPOOL_CACHE_SIZE (256)

#define PACKET_BUF_SIZE (1518)

static struct rte_mempool *pktmbuf_pool = NULL;
static int tx_idx = 0;
static struct rte_mbuf *tx_mbufs[MAX_PKT_BURST] = { 0 };

static char *httpbuf;
static size_t httpdatalen;

static void tx_flush(void)
{
	int xmit = tx_idx, xmitted = 0;
	while (xmitted != xmit)
		xmitted += rte_eth_tx_burst(0 /* port id */, 0 /* queue id */, &tx_mbufs[xmitted], xmit - xmitted);
	tx_idx = 0;
}

static err_t low_level_output(struct netif *netif __attribute__((unused)), struct pbuf *p)
{
	char buf[PACKET_BUF_SIZE];
	void *bufptr, *largebuf = NULL;
	if (sizeof(buf) < p->tot_len) {
		largebuf = (char *) malloc(p->tot_len);
		assert(largebuf);
		bufptr = largebuf;
	} else
		bufptr = buf;

	pbuf_copy_partial(p, bufptr, p->tot_len, 0);

	assert((tx_mbufs[tx_idx] = rte_pktmbuf_alloc(pktmbuf_pool)) != NULL);
	assert(p->tot_len <= RTE_MBUF_DEFAULT_BUF_SIZE);
	rte_memcpy(rte_pktmbuf_mtod(tx_mbufs[tx_idx], void *), bufptr, p->tot_len);
	rte_pktmbuf_pkt_len(tx_mbufs[tx_idx]) = rte_pktmbuf_data_len(tx_mbufs[tx_idx]) = p->tot_len;
	if (++tx_idx == MAX_PKT_BURST)
		tx_flush();

	if (largebuf)
		free(largebuf);
	return ERR_OK;
}

static err_t tcp_recv_handler(void *arg __attribute__((unused)), struct tcp_pcb *tpcb,
			      struct pbuf *p, err_t err)
{
	char buf[4] = { 0 };
	if (err != ERR_OK)
		return err;
	if (!p) {
		tcp_close(tpcb);
		return ERR_OK;
	}
	pbuf_copy_partial(p, buf, 3, 0);
	if (!strncmp(buf, "GET", 3)) {
		assert(tcp_sndbuf(tpcb) >= httpdatalen);
		assert(tcp_write(tpcb, httpbuf, httpdatalen, TCP_WRITE_FLAG_COPY) == ERR_OK);
		assert(tcp_output(tpcb) == ERR_OK);
	}
	tcp_recved(tpcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

static err_t accept_handler(void *arg __attribute__((unused)), struct tcp_pcb *tpcb, err_t err)
{
	if (err != ERR_OK)
		return err;

	tcp_recv(tpcb, tcp_recv_handler);
	tcp_setprio(tpcb, TCP_PRIO_MAX);

	tpcb->so_options |= SOF_KEEPALIVE;
	tpcb->keep_intvl = (60 * 1000);
	tpcb->keep_idle = (60 * 1000);
	tpcb->keep_cnt = 1;

	return err;
}

static uint8_t _mac[6];
static uint16_t _mtu;

static err_t if_init(struct netif *netif)
{
	for (int i = 0; i < 6; i++)
		netif->hwaddr[i] = _mac[i];
	netif->mtu = _mtu;
	netif->output = etharp_output;
	netif->linkoutput = low_level_output;
	netif->hwaddr_len = 6;
	netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_IGMP;
	return ERR_OK;
}

int main(int argc, char* const* argv)
{
	struct netif _netif = { 0 };
	ip4_addr_t _addr, _mask, _gate;
	size_t content_len = 1;
	int server_port = 10000;

	/* setting up dpdk */
	{
		int ret;
		uint16_t nb_rxd = NUM_SLOT;
		uint16_t nb_txd = NUM_SLOT;

		assert((ret = rte_eal_init(argc, (char **) argv)) >= 0);
		argc -= ret;
		argv += ret;

		assert(rte_eth_dev_count_avail() == 1);

		assert((pktmbuf_pool = rte_pktmbuf_pool_create("mbuf_pool",
					RTE_MAX(1 /* nb_ports */ * (nb_rxd + nb_txd + MAX_PKT_BURST + 1 * MEMPOOL_CACHE_SIZE), 8192),
					MEMPOOL_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE,
					rte_socket_id())) != NULL);

		{
			struct rte_eth_dev_info dev_info;
			struct rte_eth_conf local_port_conf = { 0 };

			assert(rte_eth_dev_info_get(0 /* port id */, &dev_info) >= 0);

			assert(rte_eth_dev_configure(0 /* port id */, 1 /* num queues */, 1 /* num queues */, &local_port_conf) >= 0);

			assert(rte_eth_dev_adjust_nb_rx_tx_desc(0 /* port id */, &nb_rxd, &nb_txd) >= 0);

			{
				struct rte_ether_addr ports_eth_addr;
				assert(rte_eth_macaddr_get(0 /* port id */, &ports_eth_addr) >= 0);
				for (int i = 0; i < 6; i++)
					_mac[i] = ports_eth_addr.addr_bytes[i];
			}

			assert(rte_eth_dev_get_mtu(0 /* port id */, &_mtu) >= 0);
			assert(_mtu <= PACKET_BUF_SIZE);

			assert(rte_eth_rx_queue_setup(0 /* port id */, 0 /* queue */, nb_rxd,
						rte_eth_dev_socket_id(0 /* port id */),
						&dev_info.default_rxconf,
						pktmbuf_pool) >= 0);

			assert(rte_eth_tx_queue_setup(0 /* port id */, 0 /* queue */, nb_txd,
						rte_eth_dev_socket_id(0 /* port id */),
						&dev_info.default_txconf) >= 0);

			assert(rte_eth_dev_start(0 /* port id */) >= 0);
			assert(rte_eth_promiscuous_enable(0 /* port id */) >= 0);
		}
	}

	/* parse other arg */
	{
		int ch;
		bool _a = false, _g = false, _m = false;
		while ((ch = getopt(argc, argv, "a:g:l:m:p:")) != -1) {
			switch (ch) {
			case 'a':
				inet_pton(AF_INET, optarg, &_addr);
				_a = true;
				break;
			case 'g':
				inet_pton(AF_INET, optarg, &_gate);
				_g = true;
				break;
			case 'm':
				inet_pton(AF_INET, optarg, &_mask);
				_m = true;
				break;
			case 'l':
				content_len = atol(optarg);
				break;
			case 'p':
				server_port = atoi(optarg);
				break;
			default:
				assert(0);
				break;
			}
		}
		assert(_a && _g && _m);
	}

	/* setting up lwip */
	{
		lwip_init();
		assert(netif_add(&_netif, &_addr, &_mask, &_gate, NULL, if_init, ethernet_input) != NULL);
		netif_set_default(&_netif);
		netif_set_link_up(&_netif);
		netif_set_up(&_netif);
	}

	/* main procedure */
	{
		struct tcp_pcb *tpcb, *_tpcb;
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

		assert((_tpcb = tcp_new()) != NULL);
		assert(tcp_bind(_tpcb, IP_ADDR_ANY, server_port) == ERR_OK);
		assert((tpcb = tcp_listen(_tpcb)) != NULL);
		tcp_accept(tpcb, accept_handler);

		printf("-- application has started --\n");
		/* primary loop */
		while (1) {
			struct rte_mbuf *rx_mbufs[MAX_PKT_BURST];
			unsigned short i, nb_rx = rte_eth_rx_burst(0 /* port id */, 0 /* queue id */, rx_mbufs, MAX_PKT_BURST);
			for (i = 0; i < nb_rx; i++) {
				{
					struct pbuf *p;
					assert((p = pbuf_alloc(PBUF_RAW, rte_pktmbuf_pkt_len(rx_mbufs[i]), PBUF_POOL)) != NULL);
					pbuf_take(p, rte_pktmbuf_mtod(rx_mbufs[i], void *), rte_pktmbuf_pkt_len(rx_mbufs[i]));
					p->len = p->tot_len = rte_pktmbuf_pkt_len(rx_mbufs[i]);
					assert(_netif.input(p, &_netif) == ERR_OK);
				}
				rte_pktmbuf_free(rx_mbufs[i]);
			}
			tx_flush();
			sys_check_timeouts();
		}
	}

	return 0;
}
