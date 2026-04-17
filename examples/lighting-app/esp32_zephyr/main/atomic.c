/*
 * Software atomic operations for RISC-V without A extension.
 * GCC generates __atomic_* calls when hardware atomics unavailable.
 * Zephyr SDK doesn't ship libatomic, so we provide minimal implementations.
 * These use interrupt masking for atomicity (safe on single-core ESP32-C3).
 */

#include <zephyr/kernel.h>
#include <zephyr/irq.h>

unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memorder)
{
    unsigned int key = irq_lock();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old + val;
    irq_unlock(key);
    return old;
}

_Bool __atomic_compare_exchange_4(volatile void *ptr, void *expected,
                                   unsigned int desired, _Bool weak,
                                   int success_memorder, int failure_memorder)
{
    unsigned int key = irq_lock();
    unsigned int old = *(volatile unsigned int *)ptr;
    unsigned int exp = *(unsigned int *)expected;
    _Bool success = (old == exp);
    if (success) {
        *(volatile unsigned int *)ptr = desired;
    } else {
        *(unsigned int *)expected = old;
    }
    irq_unlock(key);
    return success;
}

unsigned int __atomic_fetch_and_4(volatile void *ptr, unsigned int val, int memorder)
{
    unsigned int key = irq_lock();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old & val;
    irq_unlock(key);
    return old;
}

unsigned int __atomic_fetch_or_4(volatile void *ptr, unsigned int val, int memorder)
{
    unsigned int key = irq_lock();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = old | val;
    irq_unlock(key);
    return old;
}

unsigned int __atomic_exchange_4(volatile void *ptr, unsigned int val, int memorder)
{
    unsigned int key = irq_lock();
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = val;
    irq_unlock(key);
    return old;
}
