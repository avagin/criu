#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <linux/sockios.h>
#include "soccr.h"

#ifndef SIOCOUTQNSD
/* MAO - Define SIOCOUTQNSD ioctl if we don't have it */
#define SIOCOUTQNSD     0x894B
#endif

static void (*log)(unsigned int loglevel, const char *format, ...)
	__attribute__ ((__format__ (__printf__, 2, 3)));
static unsigned int log_level = 0;

void libsoccr_set_log(unsigned int level, void (*fn)(unsigned int level, const char *fmt, ...))
{
	log_level = level;
	log = fn;
}

#define loge(msg, ...) do { if (log && (log_level >= SOCCR_LOG_ERR)) log(SOCCR_LOG_ERR, msg, ##__VA_ARGS__); } while (0)
#define logd(msg, ...) do { if (log && (log_level >= SOCCR_LOG_DBG)) log(SOCCR_LOG_DBG, msg, ##__VA_ARGS__); } while (0)

static int tcp_repair_on(int fd)
{
	int ret, aux = 1;

	ret = setsockopt(fd, SOL_TCP, TCP_REPAIR, &aux, sizeof(aux));
	if (ret < 0)
		loge("Can't turn TCP repair mode ON\n");

	return ret;
}

static void tcp_repair_off(int fd)
{
	int aux = 0, ret;

	ret = setsockopt(fd, SOL_TCP, TCP_REPAIR, &aux, sizeof(aux));
	if (ret < 0)
		loge("Failed to turn off repair mode on socket: %m\n");
}

struct libsoccr_sk {
	int fd;
	char *recv_queue;
	char *send_queue;
};

struct libsoccr_sk *libsoccr_pause(int fd)
{
	struct libsoccr_sk *ret;

	ret = malloc(sizeof(*ret));
	if (!ret)
		return NULL;

	if (tcp_repair_on(fd) < 0) {
		free(ret);
		return NULL;
	}

	ret->recv_queue = NULL;
	ret->send_queue = NULL;
	ret->fd = fd;
	return ret;
}

void libsoccr_resume(struct libsoccr_sk *sk)
{
	tcp_repair_off(sk->fd);
	free(sk->send_queue);
	free(sk->recv_queue);
	free(sk);
}

static int refresh_sk(struct libsoccr_sk *sk, struct libsoccr_sk_data *data, struct tcp_info *ti)
{
	int size;
	socklen_t olen = sizeof(*ti);

	if (getsockopt(sk->fd, SOL_TCP, TCP_INFO, ti, &olen) || olen != sizeof(*ti)) {
		loge("Failed to obtain TCP_INFO\n");
		return -1;
	}

	switch (ti->tcpi_state) {
	case TCP_ESTABLISHED:
	case TCP_CLOSE:
		break;
	default:
		loge("Unknown state %d\n", ti->tcpi_state);
		return -1;
	}

	data->state = TCP_ESTABLISHED;

	if (ioctl(sk->fd, SIOCOUTQ, &size) == -1) {
		loge("Unable to get size of snd queue\n");
		return -1;
	}

	data->outq_len = size;

	if (ioctl(sk->fd, SIOCOUTQNSD, &size) == -1) {
		loge("Unable to get size of unsent data\n");
		return -1;
	}

	data->unsq_len = size;

	if (ioctl(sk->fd, SIOCINQ, &size) == -1) {
		loge("Unable to get size of recv queue\n");
		return -1;
	}

	data->inq_len = size;

	return 0;
}

static int get_stream_options(struct libsoccr_sk *sk, struct libsoccr_sk_data *data, struct tcp_info *ti)
{
	int ret;
	socklen_t auxl;
	int val;

	auxl = sizeof(data->mss_clamp);
	ret = getsockopt(sk->fd, SOL_TCP, TCP_MAXSEG, &data->mss_clamp, &auxl);
	if (ret < 0)
		goto err_sopt;

	data->opt_mask = ti->tcpi_options;
	if (ti->tcpi_options & TCPI_OPT_WSCALE) {
		data->snd_wscale = ti->tcpi_snd_wscale;
		data->rcv_wscale = ti->tcpi_rcv_wscale;
	}

	if (ti->tcpi_options & TCPI_OPT_TIMESTAMPS) {
		auxl = sizeof(val);
		ret = getsockopt(sk->fd, SOL_TCP, TCP_TIMESTAMP, &val, &auxl);
		if (ret < 0)
			goto err_sopt;

		data->timestamp = val;
	}

	return 0;

err_sopt:
	loge("\tsockopt failed\n");
	return -1;
}

static int get_window(struct libsoccr_sk *sk, struct libsoccr_sk_data *data)
{
	struct tcp_repair_window opt;
	socklen_t optlen = sizeof(opt);

	if (getsockopt(sk->fd, SOL_TCP,
			TCP_REPAIR_WINDOW, &opt, &optlen)) {
		/* Appeared since 4.8, but TCP_repair itself is since 3.11 */
		if (errno == ENOPROTOOPT)
			return 0;

		loge("Unable to get window properties\n");
		return -1;
	}

	data->flags |= SOCCR_FLAGS_WINDOW;
	data->snd_wl1		= opt.snd_wl1;
	data->snd_wnd		= opt.snd_wnd;
	data->max_window	= opt.max_window;
	data->rcv_wnd		= opt.rcv_wnd;
	data->rcv_wup		= opt.rcv_wup;

	return 0;
}

/*
 * TCP queues sequences and their relations to the code below
 *
 *       output queue
 * net <----------------------------- sk
 *        ^       ^       ^    seq >>
 *        snd_una snd_nxt write_seq
 *
 *                     input  queue
 * net -----------------------------> sk
 *     << seq   ^       ^
 *              rcv_nxt copied_seq
 *
 *
 * inq_len  = rcv_nxt - copied_seq = SIOCINQ
 * outq_len = write_seq - snd_una  = SIOCOUTQ
 * inq_seq  = rcv_nxt
 * outq_seq = write_seq
 *
 * On restore kernel moves the option we configure with setsockopt,
 * thus we should advance them on the _len value in restore_tcp_seqs.
 *
 */

static int get_queue(int sk, int queue_id,
		__u32 *seq, __u32 len, char **bufp)
{
	int ret, aux;
	socklen_t auxl;
	char *buf;

	aux = queue_id;
	auxl = sizeof(aux);
	ret = setsockopt(sk, SOL_TCP, TCP_REPAIR_QUEUE, &aux, auxl);
	if (ret < 0)
		goto err_sopt;

	auxl = sizeof(*seq);
	ret = getsockopt(sk, SOL_TCP, TCP_QUEUE_SEQ, seq, &auxl);
	if (ret < 0)
		goto err_sopt;

	if (len) {
		/*
		 * Try to grab one byte more from the queue to
		 * make sure there are len bytes for real
		 */
		buf = malloc(len + 1);
		if (!buf)
			goto err_buf;

		ret = recv(sk, buf, len + 1, MSG_PEEK | MSG_DONTWAIT);
		if (ret != len)
			goto err_recv;
	} else
		buf = NULL;

	*bufp = buf;
	return 0;

err_sopt:
	loge("\tsockopt failed\n");
err_buf:
	return -1;

err_recv:
	loge("\trecv failed (%d, want %d)\n", ret, len);
	free(buf);
	goto err_buf;
}

/*
 * This is how much data we've had in the initial libsoccr
 */
#define SOCR_DATA_MIN_SIZE	(17 * sizeof(__u32))

int libsoccr_get_sk_data(struct libsoccr_sk *sk, struct libsoccr_sk_data *data, unsigned data_size)
{
	struct tcp_info ti;

	if (!data || data_size < SOCR_DATA_MIN_SIZE)
		return -1;

	memset(data, 0, data_size);

	if (refresh_sk(sk, data, &ti))
		return -2;

	if (get_stream_options(sk, data, &ti))
		return -3;

	if (get_window(sk, data))
		return -4;

	if (get_queue(sk->fd, TCP_RECV_QUEUE, &data->inq_seq, data->inq_len, &sk->recv_queue))
		return -4;

	if (get_queue(sk->fd, TCP_SEND_QUEUE, &data->outq_seq, data->outq_len, &sk->send_queue))
		return -5;

	return sizeof(struct libsoccr_sk_data);
}

char *libsoccr_get_queue_bytes(struct libsoccr_sk *sk, int queue_id, int steal)
{
	char **p, *ret;

	switch (queue_id) {
		case TCP_RECV_QUEUE:
			p = &sk->recv_queue;
			break;
		case TCP_SEND_QUEUE:
			p = &sk->send_queue;
			break;
		default:
			return NULL;
	}

	ret = *p;
	if (steal)
		*p = NULL;

	return ret;
}

static int set_queue_seq(struct libsoccr_sk *sk, int queue, __u32 seq)
{
	logd("\tSetting %d queue seq to %u\n", queue, seq);

	if (setsockopt(sk->fd, SOL_TCP, TCP_REPAIR_QUEUE, &queue, sizeof(queue)) < 0) {
		loge("Can't set repair queue\n");
		return -1;
	}

	if (setsockopt(sk->fd, SOL_TCP, TCP_QUEUE_SEQ, &seq, sizeof(seq)) < 0) {
		loge("Can't set queue seq\n");
		return -1;
	}

	return 0;
}

int libsoccr_set_sk_data_unbound(struct libsoccr_sk *sk,
		struct libsoccr_sk_data *data, unsigned data_size)
{
	if (!data || data_size < SOCR_DATA_MIN_SIZE)
		return -1;

	if (data->state != TCP_ESTABLISHED)
		return -1;

	if (set_queue_seq(sk, TCP_RECV_QUEUE,
				data->inq_seq - data->inq_len))
		return -2;
	if (set_queue_seq(sk, TCP_SEND_QUEUE,
				data->outq_seq - data->outq_len))
		return -3;

	return 0;
}

#ifndef TCPOPT_SACK_PERM
#define TCPOPT_SACK_PERM TCPOPT_SACK_PERMITTED
#endif

int libsoccr_set_sk_data_noq(struct libsoccr_sk *sk,
		struct libsoccr_sk_data *data, unsigned data_size)
{
	struct tcp_repair_opt opts[4];
	int onr = 0;

	if (!data || data_size < SOCR_DATA_MIN_SIZE)
		return -1;

	logd("\tRestoring TCP options\n");

	if (data->opt_mask & TCPI_OPT_SACK) {
		logd("\t\tWill turn SAK on\n");
		opts[onr].opt_code = TCPOPT_SACK_PERM;
		opts[onr].opt_val = 0;
		onr++;
	}

	if (data->opt_mask & TCPI_OPT_WSCALE) {
		logd("\t\tWill set snd_wscale to %u\n", data->snd_wscale);
		logd("\t\tWill set rcv_wscale to %u\n", data->rcv_wscale);
		opts[onr].opt_code = TCPOPT_WINDOW;
		opts[onr].opt_val = data->snd_wscale + (data->rcv_wscale << 16);
		onr++;
	}

	if (data->opt_mask & TCPI_OPT_TIMESTAMPS) {
		logd("\t\tWill turn timestamps on\n");
		opts[onr].opt_code = TCPOPT_TIMESTAMP;
		opts[onr].opt_val = 0;
		onr++;
	}

	logd("Will set mss clamp to %u\n", data->mss_clamp);
	opts[onr].opt_code = TCPOPT_MAXSEG;
	opts[onr].opt_val = data->mss_clamp;
	onr++;

	if (setsockopt(sk->fd, SOL_TCP, TCP_REPAIR_OPTIONS,
				opts, onr * sizeof(struct tcp_repair_opt)) < 0) {
		loge("Can't repair options\n");
		return -2;
	}

	if (data->opt_mask & TCPI_OPT_TIMESTAMPS) {
		if (setsockopt(sk->fd, SOL_TCP, TCP_TIMESTAMP,
				&data->timestamp, sizeof(data->timestamp)) < 0) {
			loge("Can't set timestamp\n");
			return -3;
		}
	}

	return 0;
}

int libsoccr_set_sk_data(struct libsoccr_sk *sk,
		struct libsoccr_sk_data *data, unsigned data_size)
{
	if (data->flags & SOCCR_FLAGS_WINDOW) {
		struct tcp_repair_window wopt = {
			.snd_wl1 = data->snd_wl1,
			.snd_wnd = data->snd_wnd,
			.max_window = data->max_window,
			.rcv_wnd = data->rcv_wnd,
			.rcv_wup = data->rcv_wup,
		};
	
		if (setsockopt(sk->fd, SOL_TCP, TCP_REPAIR_WINDOW, &wopt, sizeof(wopt))) {
			loge("Unable to set window parameters\n");
			return -1;
		}
	}

	return 0;
}

static int __send_queue(struct libsoccr_sk *sk, int queue, char *buf, __u32 len)
{
	int ret, err = -1, max_chunk;
	int off;

	max_chunk = len;
	off = 0;

	do {
		int chunk = len;

		if (chunk > max_chunk)
			chunk = max_chunk;

		ret = send(sk->fd, buf + off, chunk, 0);
		if (ret <= 0) {
			if (max_chunk > 1024) {
				/*
				 * Kernel not only refuses the whole chunk,
				 * but refuses to split it into pieces too.
				 *
				 * When restoring recv queue in repair mode
				 * kernel doesn't try hard and just allocates
				 * a linear skb with the size we pass to the
				 * system call. Thus, if the size is too big
				 * for slab allocator, the send just fails
				 * with ENOMEM.
				 *
				 * In any case -- try smaller chunk, hopefully
				 * there's still enough memory in the system.
				 */
				max_chunk >>= 1;
				continue;
			}

			loge("Can't restore %d queue data (%d), want (%d:%d:%d)\n",
				  queue, ret, chunk, len, max_chunk);
			goto err;
		}
		off += ret;
		len -= ret;
	} while (len);

	err = 0;
err:
	return err;
}

static int send_queue(struct libsoccr_sk *sk, int queue, char *buf, __u32 len)
{
	logd("\tRestoring TCP %d queue data %u bytes\n", queue, len);

	if (setsockopt(sk->fd, SOL_TCP, TCP_REPAIR_QUEUE, &queue, sizeof(queue)) < 0) {
		loge("Can't set repair queue\n");
		return -1;
	}

	return __send_queue(sk, queue, buf, len);
}

int libsoccr_set_queue_bytes(struct libsoccr_sk *sk, struct libsoccr_sk_data *data, unsigned data_size,
		int queue, char *buf)
{
	if (!data || data_size < SOCR_DATA_MIN_SIZE)
		return -1;

	if (queue == TCP_RECV_QUEUE)
		return send_queue(sk, TCP_RECV_QUEUE, buf, data->inq_len);

	if (queue == TCP_SEND_QUEUE) {
		__u32 len, ulen;

		/*
		 * All data in a write buffer can be divided on two parts sent
		 * but not yet acknowledged data and unsent data.
		 * The TCP stack must know which data have been sent, because
		 * acknowledgment can be received for them. These data must be
		 * restored in repair mode.
		 */
		ulen = data->unsq_len;
		len = data->outq_len - ulen;
		if (len && send_queue(sk, TCP_SEND_QUEUE, buf, len))
			return -2;

		if (ulen) {
			/*
			 * The second part of data have never been sent to outside, so
			 * they can be restored without any tricks.
			 */
			tcp_repair_off(sk->fd);
			if (__send_queue(sk, TCP_SEND_QUEUE, buf + len, ulen))
				return -3;
			if (tcp_repair_on(sk->fd))
				return -4;
		}

		return 0;
	}

	return -5;
}
