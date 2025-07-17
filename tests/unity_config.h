#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

/* Drop into debugger on test failure */
#ifdef UNITY_DEBUG_BREAK_ON_FAIL
    #if defined(__i386__) || defined(__x86_64__)
        /* x86/x64: Use int3 instruction for debugger breakpoint */
        #define UNITY_TEST_ABORT() do { __asm__ __volatile__("int3"); __builtin_unreachable(); } while(0)
    #elif defined(__arm__) || defined(__aarch64__)
        /* ARM: Use breakpoint instruction */
        #define UNITY_TEST_ABORT() do { __asm__ __volatile__("bkpt"); __builtin_unreachable(); } while(0)
    #else
        /* Generic: Use compiler builtin (works with GCC/Clang) */
        #define UNITY_TEST_ABORT() __builtin_trap()
    #endif
#endif

#endif /* UNITY_CONFIG_H */