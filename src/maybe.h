#ifndef MAYBE_H
#define MAYBE_H 1

enum MaybeVariant {
    None,
    Some,
};

#define Maybe(T) \
    struct Maybe##T { \
        enum MaybeVariant o; \
        union { \
            char none; \
            T some; \
        }u;\
    }

#define is_some(m) ((m)->o == Some)
#define is_none(m) ((m)->o == None)
#define as_ptr(m) ((m)->o == Some ? &(m)->u.some : 0)
#define Some(...) {.o = Some, .u.some = __VA_ARGS__ }
#define None() {.o = None, .u.none = 0 }
#define set_some(v, ...) {(v)->o = Some; (v)->u.some = __VA_ARGS__; }
#define set_none(v) {(v)->o = None;}
#define match_maybe(maybe, some_name, some_block, none_block) switch((maybe)->o) {\
        case Some: { \
            typeof((maybe)->u.some) *some_name = &(maybe)->u.some; \
            { some_block }\
        }; break; \
        case None: { \
            { none_block }\
        }; break; \
    }

#endif

