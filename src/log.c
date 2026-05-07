/**
 * @file log.c  baresip log → LIBBARE_EV_LOG bridge
 */

#include "libbare_internal.h"

static void log_handler(uint32_t level, const char *msg)
{
	if ((int)level > g_bare.cfg.log_level)
		return;

	libbare_event_t ev = {0};
	ev.type = LIBBARE_EV_LOG;

	/* msg points into baresip's buffer — copy before returning */
	struct libbare_queued_event *qev = mem_alloc(sizeof(*qev), NULL);
	if (!qev)
		return;

	memset(qev, 0, sizeof(*qev));
	str_ncpy(qev->buf, msg, sizeof(qev->buf));
	qev->ev.type = LIBBARE_EV_LOG;
	qev->ev.u.log.message = qev->buf;

	mtx_lock(&g_bare.ev_lock);
	list_append(&g_bare.ev_queue, &qev->le, qev);
	cnd_signal(&g_bare.ev_cond);
	mtx_unlock(&g_bare.ev_lock);

	(void)ev;
}

static struct log s_logger = {
	.h = log_handler,
};

int bare_log_init(void)
{
	log_register_handler(&s_logger);
	return 0;
}

void bare_log_close(void)
{
	log_unregister_handler(&s_logger);
}
