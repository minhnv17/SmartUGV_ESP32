#pragma once
#include <stdbool.h>
// Single-threaded deterministic scheduler used only by the speaker host tests.
typedef bool atomic_bool;
typedef unsigned atomic_uint;
typedef int atomic_int;
#define atomic_load(p) (*(p))
#define atomic_store(p,v) (*(p)=(v))
static inline unsigned host_add(unsigned *p, unsigned v) { unsigned old=*p; *p+=v; return old; }
static inline unsigned host_sub(unsigned *p, unsigned v) { unsigned old=*p; *p-=v; return old; }
static inline bool host_exchange(bool *p, bool v) { bool old=*p; *p=v; return old; }
#define atomic_fetch_add host_add
#define atomic_fetch_sub host_sub
#define atomic_exchange host_exchange
