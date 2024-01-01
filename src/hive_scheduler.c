#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include "hive_cell.h"
#include "hive_env.h"
#include "hive_scheduler.h"
#include "hive_system_lib.h"

#include <stdint.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>

#define DEFAULT_THREAD (4)
#define MAX_GLOBAL_MQ (0x10000)
#define GP(p) ((p) % MAX_GLOBAL_MQ)

struct global_queue
{
	uint32_t head;
	uint32_t tail;
	int total;
	int thread;
	struct cell *queue[MAX_GLOBAL_MQ];
	bool flag[MAX_GLOBAL_MQ];
};

struct timer
{
	uint32_t current;
	struct cell *sys;
	struct global_queue *mq;
};

static void globalmq_push (struct global_queue *q, struct cell *c)
{
	uint32_t tail = GP (__sync_fetch_and_add (&q->tail, 1));
	q->queue[tail] = c;
	__sync_synchronize ();
	q->flag[tail] = true;
	__sync_synchronize ();
}

static struct cell *globalmq_pop (struct global_queue *q)
{
	struct cell *c;
	uint32_t head = q->head;
	uint32_t head_ptr = GP (head);
	if (head_ptr == GP (q->tail))
	{
		return NULL;
	}

	if (!q->flag[head_ptr])
	{
		return NULL;
	}

	c = q->queue[head_ptr];
	if (!__sync_bool_compare_and_swap (&q->head, head, head + 1))
	{
		return NULL;
	}
	q->flag[head_ptr] = false;
	__sync_synchronize ();

	return c;
}

static void globalmq_init (struct global_queue *q, int thread)
{
	memset (q, 0, sizeof (*q));
	q->thread = thread;
}

static inline void globalmq_inc (struct global_queue *q)
{
	__sync_add_and_fetch (&q->total, 1);
}

static inline void globalmq_dec (struct global_queue *q)
{
	__sync_sub_and_fetch (&q->total, 1);
}

static int _message_dispatch (struct global_queue *q)
{
	int r;
	struct cell *c = globalmq_pop (q);
	if (c == NULL)
		return 1;
	r = cell_dispatch_message (c);
	switch (r)
	{
	case CELL_EMPTY:
	case CELL_MESSAGE:
		break;
	case CELL_QUIT:
		globalmq_dec (q);
		return 1;
	}
	globalmq_push (q, c);
	return r;
}

static uint32_t _gettime (void)
{
	uint32_t t;
	struct timeval tv;
	gettimeofday (&tv, NULL);
	t = (uint32_t) (tv.tv_sec & 0xffffff) * 100;
	t += tv.tv_usec / 10000;
	return t;
}

static void timer_init (struct timer *t, struct cell *sys, struct global_queue *mq)
{
	t->current = _gettime ();
	t->sys = sys;
	t->mq = mq;
}

static inline void send_tick (struct cell *c)
{
	cell_send (c, 0, NULL);
}

static void _updatetime (struct timer *t)
{
	int i, diff;
	uint32_t ct = _gettime ();
	if (ct > t->current)
	{
		diff = ct - t->current;
		t->current = ct;
		for (i = 0; i < diff; i++)
		{
			send_tick (t->sys);
		}
	}
}

static void *_timer (void *p)
{
	struct timer *t = p;
	for (;;)
	{
		_updatetime (t);
		usleep (2500);
		if (t->mq->total <= 1)
			return NULL;
	}
}

static void *_worker (void *p)
{
	int i, n, ret;
	struct global_queue *mq = p;
	for (;;)
	{
		n = mq->total;
		ret = 1;
		for (i = 0; i < n; i++)
		{
			ret &= _message_dispatch (mq);
			if (n < mq->total)
			{
				n = mq->total;
			}
		}
		if (ret)
		{
			usleep (1000);						//休眠1000 us ??
			if (mq->total <= 1)
				return NULL;
		}
	}
	return NULL;
}

static void _start (struct global_queue *gmq, struct timer *t)
{
	int i;
	pthread_t pid[gmq->thread + 1];

	pthread_create (&pid[0], NULL, _timer, t);

	for (i = 1; i <= gmq->thread; i++)
	{
		pthread_create (&pid[i], NULL, _worker, gmq);
	}

	for (i = 0; i <= gmq->thread; i++)
	{
		pthread_join (pid[i], NULL);
	}
}

lua_State *scheduler_newtask (lua_State * pL)
{
	struct global_queue *mq;
	lua_State *L = luaL_newstate ();
	luaL_openlibs (L);
	hive_createenv (L);

	mq = hive_copyenv (L, pL, "message_queue");
	globalmq_inc (mq);
	hive_copyenv (L, pL, "system_pointer");

	lua_newtable (L);
	lua_newtable (L);
	lua_pushliteral (L, "v");
	lua_setfield (L, -2, "__mode");
	lua_setmetatable (L, -2);
	hive_setenv (L, "cell_map");

	return L;
}

void scheduler_deletetask (lua_State * L)
{
	lua_close (L);
}

void scheduler_starttask (lua_State * L)
{
	struct global_queue *gmq;
	struct cell *c;
	hive_getenv (L, "message_queue");
	gmq = lua_touserdata (L, -1);
	lua_pop (L, 1);
	hive_getenv (L, "cell_pointer");
	c = lua_touserdata (L, -1);
	lua_pop (L, 1);
	globalmq_push (gmq, c);
}

int scheduler_start (lua_State * L)
{
	const char *system_lua, *main_lua;
	lua_State *sL;
	struct global_queue *gmq;
	int thread;
	struct cell *sys;
	struct timer *t;

	luaL_checktype (L, 1, LUA_TTABLE);
	system_lua = luaL_checkstring (L, 2);
	main_lua = luaL_checkstring (L, 3);
	lua_getfield (L, 1, "thread");
	thread = luaL_optinteger (L, -1, DEFAULT_THREAD);
	lua_pop (L, 1);

	hive_createenv (L);
	gmq = lua_newuserdata (L, sizeof (*gmq));
	globalmq_init (gmq, thread);

	lua_pushvalue (L, -1);
	hive_setenv (L, "message_queue");

	sL = scheduler_newtask (L);
	luaL_requiref (sL, "cell.system", cell_system_lib, 0);
	lua_pop (sL, 1);

	lua_pushstring (sL, main_lua);
	lua_setglobal (sL, "maincell");

	sys = cell_new (sL, system_lua);
	if (sys == NULL)
	{
		return 0;
	}
	scheduler_starttask (sL);

	t = lua_newuserdata (L, sizeof (*t));
	timer_init (t, sys, gmq);

	_start (gmq, t);
	cell_close (sys);

	return 0;
}
