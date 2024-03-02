#ifndef _RPC_RDMA_H_
#define _RPC_RDMA_H_
#include <rdma/rdma_cma.h>
#include <sys/types.h>
#include <semaphore.h>
#include <stdatomic.h>
#include "thpool.h"

/*
 * These states are used to signal events between the completion handler
 * and the main client or server thread.
 *
 * Once CONNECTED, they cycle through RDMA_READ_ADV, RDMA_WRITE_ADV, 
 * and RDMA_WRITE_COMPLETE for each ping.
 */
enum rdma_ch_state {
	IDLE = 1,
	CONNECT_REQUEST,
	ADDR_RESOLVED,
	ROUTE_RESOLVED,
	CONNECTED,
	WORKING,
	DISCONNECTED,
	ERROR
};

// Used when creating RDMA channel.
struct rdma_ch_attr {
	int port;
	int server; /* 0 iff client */
	char ip_addr[16]; // Target server ip addr. (required by client)
	int msgbuf_cnt; // The number of msg buffers.
	int msgdata_size; // The size of a message data.
	void (*rpc_msg_handler_cb)(
		void *rpc_param); // rpc layer callback function.
	void (*user_msg_handler_cb)(void *param); // user callback function.
	threadpool msg_handler_thpool; // threadpool to execute msg handler fn.
};

// Message format including header.
// The size of message (headers + data buffer) == cb->msgbuf_size.
struct __attribute__((__packed__)) rdma_msg {
	__be64 seq_num; // sequence number.
	__be64 rpc_ch_addr; // Client's rpc_ch_info address.
	__be64 sem_addr; // Client's semaphore address.
	char data[]; // Data. Flexible array.
};

/** Per message buffer context. */
struct msgbuf_ctx {
	int id;

	struct ibv_recv_wr rq_wr; /* recv work request record */
	struct ibv_sge recv_sgl; /* recv single SGE */
	struct rdma_msg *recv_buf; /* malloc'd buffer */
	struct ibv_mr *recv_mr; /* MR associated with this buffer */

	struct ibv_send_wr sq_wr; /* send work request record */
	struct ibv_sge send_sgl;
	struct rdma_msg *send_buf; /* single send buf */
	struct ibv_mr *send_mr;

	struct ibv_send_wr rdma_sq_wr; /* rdma work request record */
	struct ibv_sge rdma_sgl; /* rdma single SGE */
	char *rdma_buf; /* used as rdma sink */
	struct ibv_mr *rdma_mr;

	// sem_t msgbuf_sem;

	// TODO: Are they required?
	// For storing requestor's (client's) info.
	uint32_t remote_rkey; /* remote guys RKEY */
	uint64_t remote_addr; /* remote guys TO */
	uint32_t remote_len; /* remote guys LEN */

	atomic_ulong
		seqn; // TODO: It doesn't need to be atomic. Only one thread accesses it.
};

/** RDMA channel control block (per connection) */
struct rdma_ch_cb {
	int server; /* 0 iff client */
	pthread_t cqthread;
	pthread_t server_thread;
	pthread_t server_daemon;
	struct ibv_comp_channel *channel;
	struct ibv_cq *cq;
	struct ibv_pd *pd;
	struct ibv_qp *qp;

	int msgbuf_cnt; // Total number of msg buffers.
	int msgbuf_size; // A size of a msg buffer including headers. msgbuf = [msgheader]+[msgdata]
	int msgheader_size; // A size of a header in a msg (msg buffer size - data size).
	int msgdata_size; // A size of data in a msg (msg buffer size - header size). Given by user.
	struct msgbuf_ctx *buf_ctxs;
	void (*rpc_msg_handler_cb)(void *rpc_pa); // rpc layer callback.
	void (*user_msg_handler_cb)(void *param); // user's msg handler callback.
	threadpool msg_handler_thpool; // threadpool to execute msg handler fn.

	enum rdma_ch_state state; /* used for cond/signalling */
	sem_t sem;

	struct sockaddr_storage sin;
	struct sockaddr_storage ssource;
	__be16 port; /* dst port in NBO */
	int size; /* ping data size */ // TODO: Not required.
	int validate; /* validate ping data */ // TODO: Not required.

	/* CM stuff */
	pthread_t cmthread;
	struct rdma_event_channel *cm_channel;
	struct rdma_cm_id *cm_id; /* connection on client side,*/
	/* listener on service side. */
	struct rdma_cm_id *child_cm_id; /* connection on server side */
};

struct rdma_ch_cb *init_rdma_ch(struct rdma_ch_attr *attr);

/**
 * @brief 
 * 
 * @param cb 
 * @param data 
 * @param sem 
 * @param msgbuf_id 
 * @param seqn If 0 is passed, allocate new number (usually by client).
 * @return int Size of sent data.
 */
int send_rdma_msg(struct rdma_ch_cb *cb, void *rpc_ch_addr, char *data,
		  sem_t *sem, int msgbuf_id, uint64_t seqn);
void destroy_rdma_client(struct rdma_ch_cb *cb);

#endif
