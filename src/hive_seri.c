#include <stdlib.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <alloca.h>

#include "lua.h"
#include "lauxlib.h"

#include "hive_seri.h"
#include "hive_cell.h"

#define TYPE_NIL (0)
#define TYPE_BOOLEAN (1)
// hibits 0 false 1 true
#define TYPE_NUMBER (2)
// hibits 0 : 0 , 1: byte, 2:word, 4: dword, 8 : double
#define TYPE_USERDATA (3)
#define TYPE_SHORT_STRING (4)
// hibits 0~31 : len
#define TYPE_LONG_STRING (5)
#define TYPE_TABLE (6)
#define TYPE_CELL (7)

#define MAX_COOKIE (32)
#define COMBINE_TYPE(t,v) ((t) | (v) << 3)

#define BLOCK_SIZE (128)
#define MAX_DEPTH (32)

struct block
{
	struct block *next;
	char buffer[BLOCK_SIZE];
};

struct write_block
{
	struct block *head;
	int len;
	struct block *current;
	int ptr;
};

struct read_block
{
	char *buffer;
	struct block *current;
	int len;
	int ptr;
};

inline static struct block *blk_alloc (void)
{
	struct block *b = malloc (sizeof (struct block));
	b->next = NULL;
	return b;
}

inline static void wb_push (struct write_block *b, const void *buf, int sz)
{
	int copy;
	const char *buffer = buf;
	if (b->ptr == BLOCK_SIZE)
	{
	_again:
		b->current = b->current->next = blk_alloc ();
		b->ptr = 0;
	}
	if (b->ptr <= BLOCK_SIZE - sz)
	{
		memcpy (b->current->buffer + b->ptr, buffer, sz);
		b->ptr += sz;
		b->len += sz;
	}
	else
	{
		copy = BLOCK_SIZE - b->ptr;
		memcpy (b->current->buffer + b->ptr, buffer, copy);
		buffer += copy;
		b->len += copy;
		sz -= copy;
		goto _again;
	}
}

static void wb_init (struct write_block *wb, struct block *b)
{
	int *plen, sz;
	if (b == NULL)
	{
		wb->head = blk_alloc ();
		wb->len = 0;
		wb->current = wb->head;
		wb->ptr = 0;
		wb_push (wb, &wb->len, sizeof (wb->len));
	}
	else
	{
		wb->head = b;
		plen = (int *) b->buffer;
		sz = *plen;
		wb->len = sz;
		while (b->next)
		{
			sz -= BLOCK_SIZE;
			b = b->next;
		}
		wb->current = b;
		wb->ptr = sz;
	}
}

static struct block *wb_close (struct write_block *b)
{
	b->current = b->head;
	b->ptr = 0;
	wb_push (b, &b->len, sizeof (b->len));
	b->current = NULL;
	return b->head;
}

static void wb_free (struct write_block *wb)
{
	struct block *next, *blk = wb->head;
	while (blk)
	{
		next = blk->next;
		free (blk);
		blk = next;
	}
	wb->head = NULL;
	wb->current = NULL;
	wb->ptr = 0;
	wb->len = 0;
}

static int rb_init (struct read_block *rb, struct block *b)
{
	rb->buffer = NULL;
	rb->current = b;
	memcpy (&(rb->len), b->buffer, sizeof (rb->len));
	rb->ptr = sizeof (rb->len);
	rb->len -= rb->ptr;
	return rb->len;
}

static void *rb_read (struct read_block *rb, void *buffer, int sz)
{
	int copy, ptr;
	struct block *next;
	void *ret;
	char *tmp;
	if (rb->len < sz)
	{
		return NULL;
	}

	if (rb->buffer)
	{
		ptr = rb->ptr;
		rb->ptr += sz;
		rb->len -= sz;
		return rb->buffer + ptr;
	}

	if (rb->ptr == BLOCK_SIZE)
	{
		next = rb->current->next;
		free (rb->current);
		rb->current = next;
		rb->ptr = 0;
	}

	copy = BLOCK_SIZE - rb->ptr;

	if (sz <= copy)
	{
		ret = rb->current->buffer + rb->ptr;
		rb->ptr += sz;
		rb->len -= sz;
		return ret;
	}

	tmp = buffer;
	memcpy (tmp, rb->current->buffer + rb->ptr, copy);
	sz -= copy;
	tmp += copy;
	rb->len -= copy;

	for (;;)
	{
		next = rb->current->next;
		free (rb->current);
		rb->current = next;

		if (sz < BLOCK_SIZE)
		{
			memcpy (tmp, rb->current->buffer, sz);
			rb->ptr = sz;
			rb->len -= sz;
			return buffer;
		}
		memcpy (tmp, rb->current->buffer, BLOCK_SIZE);
		sz -= BLOCK_SIZE;
		tmp += BLOCK_SIZE;
		rb->len -= BLOCK_SIZE;
	}
}

static void rb_close (struct read_block *rb)
{
	struct block *next;
	while (rb->current)
	{
		next = rb->current->next;
		free (rb->current);
		rb->current = next;
	}
	rb->len = 0;
	rb->ptr = 0;
}

static inline void wb_nil (struct write_block *wb)
{
	int n = TYPE_NIL;
	wb_push (wb, &n, 1);
}

static inline void wb_boolean (struct write_block *wb, int boolean)
{
	int n = COMBINE_TYPE (TYPE_BOOLEAN, boolean ? 1 : 0);
	wb_push (wb, &n, 1);
}

static inline void wb_integer (struct write_block *wb, int v)
{
	int n;
	uint16_t word;
	uint8_t byte;
	if (v == 0)
	{
		n = COMBINE_TYPE (TYPE_NUMBER, 0);
		wb_push (wb, &n, 1);
	}
	else if (v < 0)
	{
		n = COMBINE_TYPE (TYPE_NUMBER, 4);
		wb_push (wb, &n, 1);
		wb_push (wb, &v, 4);
	}
	else if (v < 0x100)
	{
		n = COMBINE_TYPE (TYPE_NUMBER, 1);
		wb_push (wb, &n, 1);
		byte = (uint8_t) v;
		wb_push (wb, &byte, 1);
	}
	else if (v < 0x10000)
	{
		n = COMBINE_TYPE (TYPE_NUMBER, 2);
		wb_push (wb, &n, 1);
		word = (uint16_t) v;
		wb_push (wb, &word, 2);
	}
	else
	{
		n = COMBINE_TYPE (TYPE_NUMBER, 4);
		wb_push (wb, &n, 1);
		wb_push (wb, &v, 4);
	}
}

static inline void wb_number (struct write_block *wb, double v)
{
	int n = COMBINE_TYPE (TYPE_NUMBER, 8);
	wb_push (wb, &n, 1);
	wb_push (wb, &v, 8);
}

static inline void wb_pointer (struct write_block *wb, void *v, int type)
{
	int n = type;
	wb_push (wb, &n, 1);
	wb_push (wb, &v, sizeof (v));
}

static inline void wb_string (struct write_block *wb, const char *str, int len)
{
	int n;
	uint32_t x32;
	uint16_t x16;
	if (len < MAX_COOKIE)
	{
		n = COMBINE_TYPE (TYPE_SHORT_STRING, len);
		wb_push (wb, &n, 1);
		if (len > 0)
		{
			wb_push (wb, str, len);
		}
	}
	else
	{
		if (len < 0x10000)
		{
			n = COMBINE_TYPE (TYPE_LONG_STRING, 2);
			wb_push (wb, &n, 1);
			x16 = (uint16_t) len;
			wb_push (wb, &x16, 2);
		}
		else
		{
			n = COMBINE_TYPE (TYPE_LONG_STRING, 4);
			wb_push (wb, &n, 1);
			x32 = (uint32_t) len;
			wb_push (wb, &x32, 4);
		}
		wb_push (wb, str, len);
	}
}

static void _pack_one (lua_State * L, struct write_block *b, int index, int depth);

static int wb_table_array (lua_State * L, struct write_block *wb, int index, int depth)
{
	int i, n, array_size = lua_rawlen (L, index);
	if (array_size >= MAX_COOKIE - 1)
	{
		n = COMBINE_TYPE (TYPE_TABLE, MAX_COOKIE - 1);
		wb_push (wb, &n, 1);
		wb_integer (wb, array_size);
	}
	else
	{
		n = COMBINE_TYPE (TYPE_TABLE, array_size);
		wb_push (wb, &n, 1);
	}

	for (i = 1; i <= array_size; i++)
	{
		lua_rawgeti (L, index, i);
		_pack_one (L, wb, -1, depth);
		lua_pop (L, 1);
	}

	return array_size;
}

static void wb_table_hash (lua_State * L, struct write_block *wb, int index, int depth, int array_size)
{
	lua_Number k;
	int32_t x;
	lua_pushnil (L);
	while (lua_next (L, index) != 0)
	{
		if (lua_type (L, -2) == LUA_TNUMBER)
		{
			k = lua_tonumber (L, -2);
			x = (int32_t) lua_tointeger (L, -2);
			if (k == (lua_Number) x && x > 0 && x <= array_size)
			{
				lua_pop (L, 1);
				continue;
			}
		}
		_pack_one (L, wb, -2, depth);
		_pack_one (L, wb, -1, depth);
		lua_pop (L, 1);
	}
	wb_nil (wb);
}

static void wb_table (lua_State * L, struct write_block *wb, int index, int depth)
{
	int array_size;
	if (index < 0)
	{
		index = lua_gettop (L) + index + 1;
	}
	array_size = wb_table_array (L, wb, index, depth);
	wb_table_hash (L, wb, index, depth, array_size);
}

static void _pack_one (lua_State * L, struct write_block *b, int index, int depth)
{
	int type;
	int32_t x;
	lua_Number n;
	size_t sz;
	const char *str;
	struct cell *c;
	if (depth > MAX_DEPTH)
	{
		wb_free (b);
		luaL_error (L, "serialize can't pack too depth table");
	}
	type = lua_type (L, index);
	switch (type)
	{
	case LUA_TNIL:
		wb_nil (b);
		break;
	case LUA_TNUMBER:
		{
			x = (int32_t) lua_tointeger (L, index);
			n = lua_tonumber (L, index);
			if ((lua_Number) x == n)
			{
				wb_integer (b, x);
			}
			else
			{
				wb_number (b, n);
			}
			break;
		}
	case LUA_TBOOLEAN:
		wb_boolean (b, lua_toboolean (L, index));
		break;
	case LUA_TSTRING:
		{
			sz = 0;
			str = lua_tolstring (L, index, &sz);
			wb_string (b, str, (int) sz);
			break;
		}
	case LUA_TLIGHTUSERDATA:
		wb_pointer (b, lua_touserdata (L, index), TYPE_USERDATA);
		break;
	case LUA_TTABLE:
		wb_table (L, b, index, depth + 1);
		break;
	case LUA_TUSERDATA:
		{
			c = cell_fromuserdata (L, index);
			if (c)
			{
				cell_grab (c);
				wb_pointer (b, c, TYPE_CELL);
				break;
			}
			// else go through
		}
	default:
		wb_free (b);
		luaL_error (L, "Unsupport type %s to serialize", lua_typename (L, type));
	}
}

static void _pack_from (lua_State * L, struct write_block *b, int from)
{
	int i, n = lua_gettop (L) - from;
	for (i = 1; i <= n; i++)
	{
		_pack_one (L, b, from + i, 0);
	}
}

int data_pack (lua_State * L)
{
	struct write_block b;
	struct block *ret;
	wb_init (&b, NULL);
	_pack_from (L, &b, 0);
	ret = wb_close (&b);
	lua_pushlightuserdata (L, ret);
	return 1;
}

static inline void __invalid_stream (lua_State * L, struct read_block *rb, int line)
{
	int len = rb->len;
	if (rb->buffer == NULL)
	{
		rb_close (rb);
	}
	luaL_error (L, "Invalid serialize stream %d (line:%d)", len, line);
}

#define _invalid_stream(L,rb) __invalid_stream(L,rb,__LINE__)

static double _get_number (lua_State * L, struct read_block *rb, int cookie)
{
	uint8_t n8, *pn8;
	uint16_t n16, *pn16;
	int n, *pn;
	double nd, *pnd;
	switch (cookie)
	{
	case 0:
		return 0;
	case 1:
		{
			n8 = 0;
			pn8 = rb_read (rb, &n8, 1);
			if (pn8 == NULL)
				_invalid_stream (L, rb);
			return *pn8;
		}
	case 2:
		{
			n16 = 0;
			pn16 = rb_read (rb, &n16, 2);
			if (pn16 == NULL)
				_invalid_stream (L, rb);
			return *pn16;
		}
	case 4:
		{
			n = 0;
			pn = rb_read (rb, &n, 4);
			if (pn == NULL)
				_invalid_stream (L, rb);
			return *pn;
		}
	case 8:
		{
			nd = 0;
			pnd = rb_read (rb, &nd, 8);
			if (pnd == NULL)
				_invalid_stream (L, rb);
			return *pnd;
		}
	default:
		_invalid_stream (L, rb);
		return 0;
	}
}

static void *_get_pointer (lua_State * L, struct read_block *rb)
{
	void *userdata = 0;
	void **v = (void **) rb_read (rb, &userdata, sizeof (userdata));
	if (v == NULL)
	{
		_invalid_stream (L, rb);
	}
	return *v;
}

static void _get_buffer (lua_State * L, struct read_block *rb, int len)
{
	char tmp[len];              //这样做不会有问题?
	//void *tmp = alloca (len); //直接在栈中分配内存
	char *p = rb_read (rb, tmp, len);
	lua_pushlstring (L, p, len);
}

static void _unpack_one (lua_State * L, struct read_block *rb, int table_index);

static void _unpack_table (lua_State * L, struct read_block *rb, int array_size, int table_index)
{
	int i;
	uint8_t type, *t;
	if (array_size == MAX_COOKIE - 1)
	{
		type = 0;
		t = rb_read (rb, &type, 1);
		if (t == NULL || (*t & 7) != TYPE_NUMBER)
		{
			_invalid_stream (L, rb);
		}
		array_size = (int) _get_number (L, rb, *t >> 3);
	}
	lua_createtable (L, array_size, 0);

	for (i = 1; i <= array_size; i++)
	{
		_unpack_one (L, rb, table_index);
		lua_rawseti (L, -2, i);
	}
	for (;;)
	{
		_unpack_one (L, rb, table_index);
		if (lua_isnil (L, -1))
		{
			lua_pop (L, 1);
			return;
		}
		_unpack_one (L, rb, table_index);
		lua_rawset (L, -3);
	}
}

static void _push_value (lua_State * L, struct read_block *rb, int type, int cookie, int table_index)
{
	uint32_t len, *plen32;
	uint16_t *plen16;
	struct cell *c;
	switch (type)
	{
	case TYPE_NIL:
		lua_pushnil (L);
		break;
	case TYPE_BOOLEAN:
		lua_pushboolean (L, cookie);
		break;
	case TYPE_NUMBER:
		lua_pushnumber (L, _get_number (L, rb, cookie));
		break;
	case TYPE_USERDATA:
		lua_pushlightuserdata (L, _get_pointer (L, rb));
		break;
	case TYPE_CELL:
		{
			c = _get_pointer (L, rb);
			cell_touserdata (L, table_index, c);
			cell_release (c);
			break;
		}
	case TYPE_SHORT_STRING:
		_get_buffer (L, rb, cookie);
		break;
	case TYPE_LONG_STRING:
		{
			if (cookie == 2)
			{
				plen16 = rb_read (rb, &len, 2);
				if (plen16 == NULL)
				{
					_invalid_stream (L, rb);
				}
				_get_buffer (L, rb, (int) *plen16);
			}
			else
			{
				if (cookie != 4)
				{
					_invalid_stream (L, rb);
				}
				plen32 = rb_read (rb, &len, 4);
				if (plen32 == NULL)
				{
					_invalid_stream (L, rb);
				}
				_get_buffer (L, rb, (int) *plen32);
			}
			break;
		}
	case TYPE_TABLE:
		{
			_unpack_table (L, rb, cookie, table_index);
			break;
		}
	}
}

static void _unpack_one (lua_State * L, struct read_block *rb, int table_index)
{
	uint8_t type = 0;
	uint8_t *t = rb_read (rb, &type, 1);
	if (t == NULL)
	{
		_invalid_stream (L, rb);
	}
	_push_value (L, rb, *t & 0x7, *t >> 3, table_index);
}

int data_unpack (lua_State * L)
{
	int i;
	uint8_t type, *t;
	struct read_block rb;
	struct block *blk = lua_touserdata (L, 1);
	if (blk == NULL)
	{
		return luaL_error (L, "Need a block to unpack");
	}
	luaL_checktype (L, 2, LUA_TTABLE);
	lua_settop (L, 2);
	rb_init (&rb, blk);

	for (i = 0;; i++)
	{
		if (i % 16 == 15)
		{
			lua_checkstack (L, i);
		}
		type = 0;
		t = rb_read (&rb, &type, 1);
		if (t == NULL)
			break;
		_push_value (L, &rb, *t & 0x7, *t >> 3, 2);
	}

	rb_close (&rb);

	return lua_gettop (L) - 2;
}
