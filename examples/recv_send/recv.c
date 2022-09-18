#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include <stdio.h>
#include <arpa/inet.h>

#define NUM_MBUFS_TMP (4096 - 1)

#define BURST_SIZE 32

int dpdk_nic_port_id = 0;		//存储dpdk使用的网卡id；

// 默认的配置
static const struct rte_eth_conf port_conf_default = {
	.rxmode = {.max_rx_pkt_len = RTE_ETHER_MAX_LEN}
};

// dpdk接管网口之前的初始化
// 检测端口是不是合法，判断端口是否可用
static void ng_init_port(struct rte_mempool *mbuf_pool) {
	// 统计可以被dpdk使用的网卡有多少
	uint16_t nb_sys_ports = rte_eth_dev_count_avail();
	// 如果为0，则说明没有可以让dpdk使用的网卡，需要设置。
	if (nb_sys_ports == 0) {
		rte_exit(EXIT_FAILURE, "No supported nic found.\n");
	}

	// 拿到可用的dpdk网卡的默认信息(还未与dpdk有关系的信息)
	struct rte_eth_dev_info dev_info;
	rte_eth_dev_info_get(dpdk_nic_port_id, &dev_info);

	//配置dpdk的网卡（rx、tx队列数量）
	const int num_rx_queues = 1;
	const int num_tx_queues = 0;
	struct rte_eth_conf port_conf = port_conf_default;
	rte_eth_dev_configure(dpdk_nic_port_id, num_rx_queues, num_tx_queues, &port_conf);

	//启动接收队列
	if (rte_eth_rx_queue_setup(dpdk_nic_port_id, 0, 128,
		rte_eth_dev_socket_id(dpdk_nic_port_id), NULL, mbuf_pool) < 0) {
		rte_exit(EXIT_FAILURE, "Could not setup RX queue.\n");
	}

	if (rte_eth_dev_start(dpdk_nic_port_id) < 0) {
		rte_exit(EXIT_FAILURE, "Could not start RX queue.\n");
	}
	
}

int main(int argc, char *argv[]) 
{
	// 初始化dpdk的环境
	// 检测巨页有没有设置，内存及cpu相关
	if (rte_eal_init(argc, argv) < 0) {
		rte_exit(EXIT_FAILURE, "Error with EAL init.\n");
	}

	// 定义内存池，dpdk一个进程确定一个内存池。
	// 要接收的数据和要发送的数据都会放在内存池里面。
	// 接下来使用的内存都是从这个内存池里面拿出来的。
	struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("mbuf pool", NUM_MBUFS_TMP,
		0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());

	if (mbuf_pool == NULL) {
		rte_exit(EXIT_FAILURE, "Error with EAL init.\n");
	}

	ng_init_port(mbuf_pool);

	while (1) {
		// 接收数据包

		struct rte_mbuf *mbufs[BURST_SIZE];   	// 内存池来的
		
		// 参数：
		// port_id 接收的网卡
		// queue_id 这个网卡的那个队列去接收数据
		unsigned int num_recvd = rte_eth_rx_burst(dpdk_nic_port_id, 0, mbufs, BURST_SIZE);
		if (num_recvd > BURST_SIZE) {
			rte_exit(EXIT_FAILURE, "Error receiving from nic.\n");
		}

		unsigned i = 0;
		for (i = 0; i < num_recvd; i++) {
			struct rte_ether_hdr *ehdr = rte_pktmbuf_mtod(mbufs[i], struct rte_ether_hdr *);
			if (ehdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
				continue;
			}

			struct rte_ipv4_hdr *iphdr = rte_pktmbuf_mtod_offset(mbufs[i], struct rte_ipv4_hdr *,
				sizeof(struct rte_ether_hdr));
			if (iphdr->next_proto_id == IPPROTO_UDP) {
				struct rte_udp_hdr *udphdr = (struct rte_udp_hdr *)(iphdr + 1);

				uint16_t length = ntohs(udphdr->dgram_len);
				*((char *)udphdr + length) = '\0';

				struct in_addr addr;
				addr.s_addr = iphdr->src_addr;
				printf("src: %s:%d, ", inet_ntoa(addr), ntohs(udphdr->src_port));

				addr.s_addr = iphdr->dst_addr;
				printf("dst: %s:%d, length: %d --> %s\n", inet_ntoa(addr), 
					ntohs(udphdr->dst_port), length, (char *)(udphdr + 1));

				rte_pktmbuf_free(mbufs[i]);
			}
		}
	}
}	

