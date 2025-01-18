#ifndef _COMPILER_H
#define _COMPILER_H

#define _GNU_SOURCE
#define BUF_SIZE 300    /* Default size for temporary, stack-allocated buffers */
#define bool unsigned char
#define false 0
#define true 1
#define STRINGIFY(x) #x
#define TOSTRING(x) STRINGIFY(x)
#define _Nullable
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

#define max(a, b)					\
		({	typeof(a) _a = (a); 	\
			typeof(b) _b = (b); 	\
			_a > _b ? _a : _b;	})

#define min(a, b)					\
		({	typeof(a) _a = (a); 	\
			typeof(b) _b = (b); 	\
			_a < _b ? _a : _b;	})


#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

#endif /* _COMPILER_H */