// SPDX-License-Identifier: GPL-2.0

#include <unistd.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <sched.h>
#include <netinet/in.h>

#include "../soccr/soccr.h"

#include "common/config.h"
#include "cr_options.h"
#include "util.h"
#include "common/list.h"
#include "log.h"
#include "files.h"
#include "sockets.h"
#include "sk-inet.h"
#include "netfilter.h"
#include "image.h"
#include "namespaces.h"
#include "xmalloc.h"
#include "kerndat.h"
#include "restorer.h"
#include "rst-malloc.h"

#include "protobuf.h"
#include "images/mptcp-stream.pb-c.h"

#undef LOG_PREFIX
#define LOG_PREFIX "mptcp: "

static LIST_HEAD(cpt_mptcp_repair_sockets);
static LIST_HEAD(rst_mptcp_repair_sockets);

static int lock_connection(struct inet_sk_desc *sk)
{
	if (opts.network_lock_method == NETWORK_LOCK_IPTABLES)
		return iptables_lock_connection(sk);
	else if (opts.network_lock_method == NETWORK_LOCK_NFTABLES)
		return nftables_lock_connection(sk);
	else if (opts.network_lock_method == NETWORK_LOCK_SKIP)
		return 0;

	return -1;
}

static int unlock_connection(struct inet_sk_desc *sk)
{
	if (opts.network_lock_method == NETWORK_LOCK_IPTABLES)
		return iptables_unlock_connection(sk);
	else if (opts.network_lock_method == NETWORK_LOCK_NFTABLES)
		return 0;
	else if (opts.network_lock_method == NETWORK_LOCK_SKIP)
		return 0;

	return -1;
}

static int mptcp_repair_established(int fd, struct inet_sk_desc *sk)
{
	int ret;
	struct libsoccr_sk *socr;

	pr_info("\tTurning MPTCP repair on for socket %x\n", sk->sd.ino);
	sk->rfd = dup(fd);
	if (sk->rfd < 0) {
		pr_perror("Can't save MPTCP socket fd for repair");
		goto err1;
	}

	if (!(root_ns_mask & CLONE_NEWNET)) {
		ret = lock_connection(sk);
		if (ret < 0) {
			pr_err("Failed to lock MPTCP connection %x\n", sk->sd.ino);
			goto err2;
		}
	}

	socr = libsoccr_mptcp_pause(sk->rfd);
	if (!socr)
		goto err3;

	sk->priv = socr;
	list_add_tail(&sk->rlist, &cpt_mptcp_repair_sockets);
	return 0;

err3:
	if (!(root_ns_mask & CLONE_NEWNET))
		unlock_connection(sk);
err2:
	close(sk->rfd);
err1:
	return -1;
}

static void mptcp_unlock_one(struct inet_sk_desc *sk)
{
	int ret;

	list_del(&sk->rlist);

	if (!(root_ns_mask & CLONE_NEWNET)) {
		ret = unlock_connection(sk);
		if (ret < 0)
			pr_err("Failed to unlock MPTCP connection %x\n", sk->sd.ino);
	}

	libsoccr_mptcp_resume(sk->priv);
	sk->priv = NULL;

	restore_opt(sk->rfd, SOL_SOCKET, SO_REUSEADDR, &sk->cpt_reuseaddr);
	close(sk->rfd);
}

void cpt_unlock_mptcp_connections(void)
{
	struct inet_sk_desc *sk, *n;

	list_for_each_entry_safe(sk, n, &cpt_mptcp_repair_sockets, rlist)
		mptcp_unlock_one(sk);
}

static int dump_mptcp_conn_state(struct inet_sk_desc *sk)
{
	struct libsoccr_sk *socr = sk->priv;
	int exit_code = -1;
	int ret;
	struct cr_img *img;
	MptcpStreamEntry mse = MPTCP_STREAM_ENTRY__INIT;
	char *buf;
	struct libsoccr_mptcp_sk_data data;

	ret = libsoccr_mptcp_save(socr, &data, sizeof(data));
	if (ret < 0) {
		pr_err("libsoccr_mptcp_save() failed with %d\n", ret);
		goto err;
	}

	sk->state = data.mptcp_state;

	mse.local_key = data.local_key;
	mse.remote_key = data.remote_key;
	mse.token = data.token;
	mse.key_flags = data.key_flags;

	mse.write_seq = data.write_seq;
	mse.snd_nxt = data.snd_nxt;
	mse.snd_una = data.snd_una;
	mse.rcv_nxt = data.rcv_nxt;
	mse.rcv_wnd_sent = data.rcv_wnd_sent;
	if (data.rcv_data_fin) {
		mse.has_rcv_data_fin_seq = true;
		mse.rcv_data_fin_seq = data.rcv_data_fin_seq;
	}
	mse.mptcp_state = data.mptcp_state;
	mse.seq_flags = 0;
	if (data.rcv_data_fin)
		mse.seq_flags |= MPTCP_SEQ_FLAG_RCV_DATA_FIN;
	if (data.snd_data_fin_enable)
		mse.seq_flags |= MPTCP_SEQ_FLAG_SND_DATA_FIN_ENABLE;
	if (data.allow_infinite_fallback)
		mse.seq_flags |= MPTCP_SEQ_FLAG_ALLOW_INFINITE_FALLBACK;

	mse.inq_len = data.inq_len;
	mse.outq_len = data.outq_len;
	mse.has_unsq_len = true;
	mse.unsq_len = data.unsq_len;

	mse.sf_snd_una = data.sf_snd_una;
	mse.sf_snd_nxt = data.sf_snd_nxt;
	mse.sf_rcv_nxt = data.sf_rcv_nxt;
	mse.sf_snd_wnd = data.sf_snd_wnd;
	mse.sf_rcv_wnd = data.sf_rcv_wnd;
	mse.sf_mss_clamp = data.sf_mss_clamp;
	mse.has_sf_ts_recent = true;
	mse.sf_ts_recent = data.sf_ts_recent;
	mse.has_sf_ts_recent_stamp = true;
	mse.sf_ts_recent_stamp = data.sf_ts_recent_stamp;
	mse.has_sf_tsoffset = true;
	mse.sf_tsoffset = data.sf_tsoffset;
	mse.has_sf_snd_wscale = true;
	mse.sf_snd_wscale = data.sf_snd_wscale;
	mse.has_sf_rcv_wscale = true;
	mse.sf_rcv_wscale = data.sf_rcv_wscale;
	mse.has_sf_sack_ok = true;
	mse.sf_sack_ok = data.sf_sack_ok;
	mse.has_sf_timestamps = true;
	mse.sf_timestamps = data.sf_timestamps;

	mse.has_sf_idsn = true;
	mse.sf_idsn = data.sf_idsn;
	mse.has_sf_map_seq = true;
	mse.sf_map_seq = data.sf_map_seq;
	mse.has_sf_map_subflow_seq = true;
	mse.sf_map_subflow_seq = data.sf_map_subflow_seq;
	mse.has_sf_ssn_offset = true;
	mse.sf_ssn_offset = data.sf_ssn_offset;
	mse.has_sf_rel_write_seq = true;
	mse.sf_rel_write_seq = data.sf_rel_write_seq;

	img = open_image(CR_FD_MPTCP_STREAM, O_DUMP, sk->sd.ino);
	if (!img)
		goto err;

	ret = pb_write_one(img, &mse, PB_MPTCP_STREAM);
	if (ret < 0)
		goto err_close;

	buf = libsoccr_get_queue_bytes(socr, TCP_RECV_QUEUE, SOCCR_MEM_EXCL);
	if (buf) {
		ret = write_img_buf(img, buf, mse.inq_len);
		if (ret < 0)
			goto err_close;
		xfree(buf);
	}

	buf = libsoccr_get_queue_bytes(socr, TCP_SEND_QUEUE, SOCCR_MEM_EXCL);
	if (buf) {
		ret = write_img_buf(img, buf, mse.outq_len);
		if (ret < 0)
			goto err_close;
		xfree(buf);
	}

	pr_info("Done MPTCP dump\n");
	exit_code = 0;
err_close:
	close_image(img);
err:
	return exit_code;
}

int dump_one_mptcp(int fd, struct inet_sk_desc *sk, SkOptsEntry *soe)
{
	if (sk->dst_port == 0)
		return 0;

	if (opts.tcp_close)
		return 0;

	pr_info("Dumping MPTCP connection\n");

	if (mptcp_repair_established(fd, sk))
		return -1;

	if (dump_mptcp_conn_state(sk))
		return -1;

	return 0;
}

static int read_mptcp_queue(struct libsoccr_sk *sk, int queue, u32 len, struct cr_img *img)
{
	char *buf;

	buf = xmalloc(len);
	if (!buf)
		return -1;

	if (read_img_buf(img, buf, len) < 0) {
		xfree(buf);
		return -1;
	}

	return libsoccr_set_queue_bytes(sk, queue, buf, SOCCR_MEM_EXCL);
}

static int restore_mptcp_conn_state(int sk, struct libsoccr_sk *socr, struct inet_sk_info *ii)
{
	struct cr_img *img;
	MptcpStreamEntry *mse;
	struct libsoccr_mptcp_sk_data data = {};
	union libsoccr_addr sa_src, sa_dst;

	pr_info("Restoring MPTCP connection id %x ino %x\n", ii->ie->id, ii->ie->ino);

	img = open_image(CR_FD_MPTCP_STREAM, O_RSTR, ii->ie->ino);
	if (!img)
		goto err;

	if (pb_read_one(img, &mse, PB_MPTCP_STREAM) < 0)
		goto err_c;

	data.local_key = mse->local_key;
	data.remote_key = mse->remote_key;
	data.token = mse->token;
	data.key_flags = mse->key_flags;

	data.write_seq = mse->write_seq;
	data.snd_nxt = mse->snd_nxt;
	data.snd_una = mse->snd_una;
	data.rcv_nxt = mse->rcv_nxt;
	data.rcv_wnd_sent = mse->rcv_wnd_sent;
	if (mse->has_rcv_data_fin_seq) {
		data.rcv_data_fin = 1;
		data.rcv_data_fin_seq = mse->rcv_data_fin_seq;
	}
	data.mptcp_state = mse->mptcp_state;
	data.snd_data_fin_enable = !!(mse->seq_flags & MPTCP_SEQ_FLAG_SND_DATA_FIN_ENABLE);
	data.allow_infinite_fallback = !!(mse->seq_flags & MPTCP_SEQ_FLAG_ALLOW_INFINITE_FALLBACK);

	data.inq_len = mse->inq_len;
	data.outq_len = mse->outq_len;
	data.unsq_len = mse->has_unsq_len ? mse->unsq_len : mse->outq_len;

	data.sf_snd_una = mse->sf_snd_una;
	data.sf_snd_nxt = mse->sf_snd_nxt;
	data.sf_rcv_nxt = mse->sf_rcv_nxt;
	data.sf_snd_wnd = mse->sf_snd_wnd;
	data.sf_rcv_wnd = mse->sf_rcv_wnd;
	data.sf_mss_clamp = mse->sf_mss_clamp;
	data.sf_ts_recent = mse->has_sf_ts_recent ? mse->sf_ts_recent : 0;
	data.sf_ts_recent_stamp = mse->has_sf_ts_recent_stamp ? mse->sf_ts_recent_stamp : 0;
	data.sf_tsoffset = mse->has_sf_tsoffset ? mse->sf_tsoffset : 0;
	data.sf_snd_wscale = mse->has_sf_snd_wscale ? mse->sf_snd_wscale : 0;
	data.sf_rcv_wscale = mse->has_sf_rcv_wscale ? mse->sf_rcv_wscale : 0;
	data.sf_sack_ok = mse->has_sf_sack_ok ? mse->sf_sack_ok : 0;
	data.sf_timestamps = mse->has_sf_timestamps ? mse->sf_timestamps : 0;

	data.sf_idsn = mse->has_sf_idsn ? mse->sf_idsn : 0;
	data.sf_map_seq = mse->has_sf_map_seq ? mse->sf_map_seq : 0;
	data.sf_map_subflow_seq = mse->has_sf_map_subflow_seq ? mse->sf_map_subflow_seq : 0;
	data.sf_ssn_offset = mse->has_sf_ssn_offset ? mse->sf_ssn_offset : 0;
	data.sf_rel_write_seq = mse->has_sf_rel_write_seq ? mse->sf_rel_write_seq : 0;

	if (restore_sockaddr(&sa_src, ii->ie->family, ii->ie->src_port, ii->ie->src_addr, 0) < 0)
		goto err_c;
	if (restore_sockaddr(&sa_dst, ii->ie->family, ii->ie->dst_port, ii->ie->dst_addr, 0) < 0)
		goto err_c;

	libsoccr_set_addr(socr, 1, &sa_src, 0);
	libsoccr_set_addr(socr, 0, &sa_dst, 0);

	if (data.inq_len && read_mptcp_queue(socr, TCP_RECV_QUEUE, data.inq_len, img))
		goto err_c;
	if (data.outq_len && read_mptcp_queue(socr, TCP_SEND_QUEUE, data.outq_len, img))
		goto err_c;

	if (libsoccr_mptcp_restore(socr, &data, sizeof(data)))
		goto err_c;

	mptcp_stream_entry__free_unpacked(mse, NULL);
	close_image(img);
	return 0;

err_c:
	mptcp_stream_entry__free_unpacked(mse, NULL);
	close_image(img);
err:
	return -1;
}

int restore_one_mptcp(int fd, struct inet_sk_info *ii)
{
	struct libsoccr_sk *sk;

	pr_info("Restoring MPTCP connection\n");

	if (opts.tcp_close) {
		if (shutdown(fd, SHUT_RDWR) && errno != ENOTCONN)
			pr_perror("Unable to shutdown MPTCP socket id %x ino %x", ii->ie->id, ii->ie->ino);
		return 0;
	}

	sk = libsoccr_mptcp_pause(fd);
	if (!sk)
		return -1;

	if (restore_mptcp_conn_state(fd, sk, ii)) {
		libsoccr_release(sk);
		return -1;
	}

	return 0;
}

void mptcp_locked_conn_add(struct inet_sk_info *ii)
{
	list_add_tail(&ii->rlist, &rst_mptcp_repair_sockets);
	ii->sk_fd = -1;
}

static int unlock_connection_info(struct inet_sk_info *si)
{
	if (opts.network_lock_method == NETWORK_LOCK_IPTABLES)
		return iptables_unlock_connection_info(si);
	else if (opts.network_lock_method == NETWORK_LOCK_NFTABLES)
		return 0;
	else if (opts.network_lock_method == NETWORK_LOCK_SKIP)
		return 0;

	return -1;
}

void rst_unlock_mptcp_connections(void)
{
	struct inet_sk_info *ii;

	if (opts.tcp_close)
		return;

	if (root_ns_mask & CLONE_NEWNET)
		return;

	list_for_each_entry(ii, &rst_mptcp_repair_sockets, rlist)
		unlock_connection_info(ii);
}

int prepare_mptcp_socks(struct task_restore_args *ta)
{
	struct inet_sk_info *ii;

	if (!ta->tcp_socks) {
		ta->tcp_socks = (struct rst_tcp_sock *)rst_mem_align_cpos(RM_PRIVATE);
		ta->tcp_socks_n = 0;
	}

	list_for_each_entry(ii, &rst_mptcp_repair_sockets, rlist) {
		struct rst_tcp_sock *rs;

		if (ii->sk_fd == -1)
			continue;

		rs = rst_mem_alloc(sizeof(*rs), RM_PRIVATE);
		if (!rs)
			return -1;

		rs->sk = ii->sk_fd;
		rs->reuseaddr = ii->ie->opts ? ii->ie->opts->reuseaddr : false;
		rs->is_mptcp = true;
		ta->tcp_socks_n++;
	}

	return 0;
}
