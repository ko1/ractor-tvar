#include "ruby/ruby.h"
#include "ruby/util.h"
#include "ruby/thread_native.h"
#include "ruby/ractor.h"

// quoted (and modified) from "internal/fixnum.h"

static inline long
rb_overflowed_fix_to_int(long x)
{
    return (long)((unsigned long)(x >> 1) ^ (1LU << (SIZEOF_LONG * CHAR_BIT - 1)));
}

static inline VALUE
rb_fix_plus_fix(VALUE x, VALUE y)
{
#if !HAVE_BUILTIN___BUILTIN_ADD_OVERFLOW
    long lz = FIX2LONG(x) + FIX2LONG(y);
    return LONG2NUM(lz);
#else
    long lz;
    /* NOTE
     * (1) `LONG2FIX(FIX2LONG(x)+FIX2LONG(y))`
     +     = `((lx*2+1)/2 + (ly*2+1)/2)*2+1`
     +     = `lx*2 + ly*2 + 1`
     +     = `(lx*2+1) + (ly*2+1) - 1`
     +     = `x + y - 1`
     * (2) Fixnum's LSB is always 1.
     *     It means you can always run `x - 1` without overflow.
     * (3) Of course `z = x + (y-1)` may overflow.
     *     At that time true value is
     *     * positive: 0b0 1xxx...1, and z = 0b1xxx...1
     *     * nevative: 0b1 0xxx...1, and z = 0b0xxx...1
     *     To convert this true value to long,
     *     (a) Use arithmetic shift
     *         * positive: 0b11xxx...
     *         * negative: 0b00xxx...
     *     (b) invert MSB
     *         * positive: 0b01xxx...
     *         * negative: 0b10xxx...
     */
    if (__builtin_add_overflow((long)x, (long)y-1, &lz)) {
        return rb_int2big(rb_overflowed_fix_to_int(lz));
    }
    else {
        return (VALUE)lz;
    }
#endif
}

static inline unsigned int
rb_popcount32(uint32_t x)
{
#if defined(_MSC_VER) && defined(__AVX__)
    /* Note: CPUs since Nehalem and Barcelona  have had this instruction so SSE
     * 4.2 should suffice, but it seems there is no such thing like __SSE_4_2__
     * predefined macro in MSVC.  They do have __AVX__ so use it instead. */
    return (unsigned int)__popcnt(x);

#elif HAVE_BUILTIN___BUILTIN_POPCOUNT
    return (unsigned int)__builtin_popcount(x);
#else
    x = (x & 0x55555555) + (x >> 1 & 0x55555555);
    x = (x & 0x33333333) + (x >> 2 & 0x33333333);
    x = (x & 0x0f0f0f0f) + (x >> 4 & 0x0f0f0f0f);
    x = (x & 0x001f001f) + (x >> 8 & 0x001f001f);
    x = (x & 0x0000003f) + (x >>16 & 0x0000003f);
    return (unsigned int)x;
#endif
}

// Thread/Ractor support transactional variable Thread::TVar

// 0: null (BUG/only for evaluation)
// 1: mutex
// TODO: 1: atomic
#define SLOT_LOCK_TYPE 1

struct slot_lock {
#if   SLOT_LOCK_TYPE == 0
#elif SLOT_LOCK_TYPE == 1
    rb_nativethread_lock_t lock;
#else
#error unknown
#endif
};

struct tvar_slot {
    uint64_t version;
    VALUE value;
    VALUE index;
    struct slot_lock lock;
};

struct tx_global {
    uint64_t version;
    rb_nativethread_lock_t version_lock;

    uint64_t slot_index;
    rb_nativethread_lock_t slot_index_lock;
};

struct tx_log {
    VALUE value;
    struct tvar_slot *slot;
    VALUE tvar; // mark slot
};

struct tx_logs {
    uint64_t version;
    uint32_t logs_cnt;
    uint32_t logs_capa;

    struct tx_log *logs;

    bool enabled;
    bool stop_adding;

    uint32_t retry_history;
    size_t retry_on_commit;
    size_t retry_on_read_lock;
    size_t retry_on_read_version;
};

static struct tx_global tx_global;

static VALUE rb_eTxRetry;
static VALUE rb_eTxError;
static VALUE rb_exc_tx_retry;
static VALUE rb_cRactorTVar;
static VALUE rb_cRactorTxLogs;

static ID id_tx_logs;

static struct tx_global *
tx_global_ptr(void)
{
    return &tx_global;
}

#define TVAR_DEBUG_LOG(...)

static VALUE
txg_next_index(struct tx_global *txg)
{
    VALUE index;
    rb_native_mutex_lock(&txg->slot_index_lock);
    {
        txg->slot_index++;
        index = INT2FIX(txg->slot_index);
    }
    rb_native_mutex_unlock(&txg->slot_index_lock);

    return index;
}

static uint64_t
txg_version(const struct tx_global *txg)
{
    uint64_t version;
    version = txg->version;
    return version;
}

static uint64_t
txg_next_version(struct tx_global *txg)
{
    uint64_t version;

    rb_native_mutex_lock(&txg->version_lock);
    {
        txg->version++;
        version = txg->version;
        TVAR_DEBUG_LOG("new_version:%lu", version);
    }
    rb_native_mutex_unlock(&txg->version_lock);

    return version;
}

// tx: transaction

static void
tx_slot_lock_init(struct slot_lock *lock)
{
#if   SLOT_LOCK_TYPE == 0
#elif SLOT_LOCK_TYPE == 1
    rb_native_mutex_initialize(&lock->lock);
#else
#error unknown
#endif
}

static void
tx_slot_lock_free(struct slot_lock *lock)
{
#if   SLOT_LOCK_TYPE == 0
#elif SLOT_LOCK_TYPE == 1
    rb_native_mutex_destroy(&lock->lock);
#else
#error unknown
#endif
}

static bool
tx_slot_lock_trylock(struct slot_lock *lock)
{
#if   SLOT_LOCK_TYPE == 0
    return true;
#elif SLOT_LOCK_TYPE == 1
    return rb_native_mutex_trylock(&lock->lock) == 0;
#else
#error unknown
#endif
}

static void
tx_slot_lock_lock(struct slot_lock *lock)
{
#if   SLOT_LOCK_TYPE == 0
#elif SLOT_LOCK_TYPE == 1
    rb_native_mutex_lock(&lock->lock);
#else
#error unknown
#endif
}

static void
tx_slot_lock_unlock(struct slot_lock *lock)
{
#if   SLOT_LOCK_TYPE == 0
#elif SLOT_LOCK_TYPE == 1
    rb_native_mutex_unlock(&lock->lock);
#else
#error unknown
#endif
}

static bool
tx_slot_trylock(struct tvar_slot *slot)
{
    return tx_slot_lock_trylock(&slot->lock);
}

static void
tx_slot_lock(struct tvar_slot *slot)
{
    tx_slot_lock_lock(&slot->lock);
}

static void
tx_slot_unlock(struct tvar_slot *slot)
{
    tx_slot_lock_unlock(&slot->lock);
}


static void
tx_mark(void *ptr)
{
    struct tx_logs *tx = (struct tx_logs *)ptr;

    for (uint32_t i=0; i<tx->logs_cnt; i++) {
        rb_gc_mark(tx->logs[i].value);
    }
}

static void
tx_free(void *ptr)
{
    struct tx_logs *tx = (struct tx_logs *)ptr;

    TVAR_DEBUG_LOG("retry %5lu commit:%lu read_lock:%lu read_version:%lu",
                   tx->retry_on_commit + tx->retry_on_read_lock + tx->retry_on_read_version,
                   tx->retry_on_commit,
                   tx->retry_on_read_lock,
                   tx->retry_on_read_version);

    ruby_xfree(tx->logs);
    ruby_xfree(tx);
}

static const rb_data_type_t txlogs_type = {
    "txlogs",
    {tx_mark, tx_free, NULL,},
    0, 0, RUBY_TYPED_FREE_IMMEDIATELY
};

static struct tx_logs *
tx_logs(void)
{
    VALUE cth = rb_thread_current();
    VALUE txobj = rb_thread_local_aref(cth, id_tx_logs);

    if (txobj == Qnil) {
        struct tx_logs *tx;
        txobj = TypedData_Make_Struct(rb_cRactorTxLogs, struct tx_logs, &txlogs_type, tx);
        tx->logs_capa = 0x10; // default
        tx->logs = ALLOC_N(struct tx_log, tx->logs_capa);
        rb_thread_local_aset(cth, id_tx_logs, txobj);
        return tx;
    }
    else {
        // TODO: check
        return DATA_PTR(txobj);
    }
}

static struct tx_log *
tx_lookup(struct tx_logs *tx, VALUE tvar)
{
    struct tx_log *copies = tx->logs;
    uint32_t cnt = tx->logs_cnt;

    for (uint32_t i = 0; i< cnt; i++) {
        if (copies[i].tvar == tvar) {
            return &copies[i];
        }
    }

    return NULL;
}

static void
tx_add(struct tx_logs *tx, VALUE val, struct tvar_slot *slot, VALUE tvar)
{
    if (RB_UNLIKELY(tx->logs_capa == tx->logs_cnt)) {
        uint32_t new_capa =  tx->logs_capa * 2;
        RB_REALLOC_N(tx->logs, struct tx_log, new_capa);
        tx->logs_capa = new_capa;
    }
    if (RB_UNLIKELY(tx->stop_adding)) {
        rb_raise(rb_eTxError, "can not handle more transactional variable: %"PRIxVALUE, rb_inspect(tvar));
    }
    struct tx_log *log = &tx->logs[tx->logs_cnt++];

    log->value = val;
    log->slot = slot;
    log->tvar = tvar;
}

static VALUE
tx_get(struct tx_logs *tx, struct tvar_slot *slot, VALUE tvar)
{
    struct tx_log *ent = tx_lookup(tx, tvar);

    if (ent == NULL) {
        VALUE val;

        if (tx_slot_trylock(slot)) {
            if (slot->version > tx->version) {
                TVAR_DEBUG_LOG("RV < slot->V slot:%u slot->version:%lu, tx->version:%lu", FIX2INT(slot->index), slot->version, tx->version);
                tx_slot_unlock(slot);
                tx->retry_on_read_version++;
                goto abort_and_retry;
            }
            val = slot->value;
            tx_slot_unlock(slot);
        }
        else {
            TVAR_DEBUG_LOG("RV < slot->V slot:%u slot->version:%lu, tx->version:%lu", FIX2INT(slot->index), slot->version, tx->version);
            tx->retry_on_read_lock++;
            goto abort_and_retry;
        }
        tx_add(tx, val, slot, tvar);
        return val;

      abort_and_retry:
        rb_raise(rb_eTxRetry, "retry");
    }
    else {
        return ent->value;
    }
}

static void
tx_set(struct tx_logs *tx, VALUE val, struct tvar_slot *slot, VALUE tvar)
{
    struct tx_log *ent = tx_lookup(tx, tvar);

    if (ent == NULL) {
        tx_add(tx, val, slot, tvar);
    }
    else {
        ent->value = val;
    }
}

static void
tx_check(struct tx_logs *tx)
{
    if (RB_UNLIKELY(!tx->enabled)) {
        rb_raise(rb_eTxError, "can not set without transaction");
    }
}

static void
tx_setup(struct tx_global *txg, struct tx_logs *tx)
{
    RUBY_ASSERT(tx->enabled);
    RUBY_ASSERT(tx->logs_cnt == 0);

    tx->version = txg_version(txg);

    TVAR_DEBUG_LOG("tx:%lu", tx->version);
}

static struct tx_logs *
tx_begin(void)
{
    struct tx_global *txg = tx_global_ptr();
    struct tx_logs *tx = tx_logs();

    RUBY_ASSERT(tx->stop_adding == false);
    RUBY_ASSERT(tx->logs_cnt == 0);

    if (tx->enabled == false) {
        tx->enabled = true;
        tx_setup(txg, tx);
        return tx;
    }
    else {
        return NULL;
    }
}

static VALUE
tx_reset(struct tx_logs *tx)
{
    struct tx_global *txg = tx_global_ptr();
    tx->logs_cnt = 0;

    // contention management (CM)
    if (tx->retry_history != 0) {
        int recent_retries = 0; rb_popcount32(tx->retry_history);
        TVAR_DEBUG_LOG("retry recent_retries:%d", recent_retries);

        struct timeval tv = {
            .tv_sec = 0,
            .tv_usec = 1 * recent_retries,
        };

        TVAR_DEBUG_LOG("CM tv_usec:%lu", (unsigned long)tv.tv_usec);
        rb_thread_wait_for(tv);
    }

    tx_setup(txg, tx);
    TVAR_DEBUG_LOG("tx:%lu", tx->version);

    return Qnil;
}

static VALUE
tx_end(struct tx_logs *tx)
{
    TVAR_DEBUG_LOG("tx:%lu", tx->version);

    RUBY_ASSERT(tx->enabled);
    RUBY_ASSERT(tx->stop_adding == false);
    tx->enabled = false;
    tx->logs_cnt = 0;
    return Qnil;
}

static void
tx_commit_release(struct tx_logs *tx, uint32_t n)
{
    struct tx_log *copies = tx->logs;

    for (uint32_t i = 0; i<n; i++) {
        struct tx_log *copy = &copies[i];
        struct tvar_slot *slot = copy->slot;
        tx_slot_unlock(slot);
    }
}

static VALUE
tx_commit(struct tx_logs *tx)
{
    struct tx_global *txg = tx_global_ptr();
    uint32_t i;
    struct tx_log *copies = tx->logs;
    uint32_t logs_cnt = tx->logs_cnt;

    for (i=0; i<logs_cnt; i++) {
        struct tx_log *copy = &copies[i];
        struct tvar_slot *slot = copy->slot;

        if (RB_LIKELY(tx_slot_trylock(slot))) {
            if (RB_UNLIKELY(slot->version > tx->version)) {
                TVAR_DEBUG_LOG("RV < slot->V slot:%lu tx:%lu rs:%lu", slot->version, tx->version, txg->version);
                tx_commit_release(tx, i+1);
                goto abort_and_retry;
            }
            else {
                // lock success
                TVAR_DEBUG_LOG("lock slot:%lu tx:%lu rs:%lu", slot->version, tx->version, txg->version);
            }
        }
        else {
            TVAR_DEBUG_LOG("trylock fail slot:%lu tx:%lu rs:%lu", slot->version, tx->version, txg->version);
            tx_commit_release(tx, i);
            goto abort_and_retry;
        }
    }

    // ok
    tx->retry_history <<= 1;

    uint64_t new_version = txg_next_version(txg);

    for (i=0; i<logs_cnt; i++) {
        struct tx_log *copy = &copies[i];
        struct tvar_slot *slot = copy->slot;

        if (slot->value != copy->value) {
            TVAR_DEBUG_LOG("write slot:%d %d->%d slot->version:%lu->%lu tx:%lu rs:%lu",
                           FIX2INT(slot->index), FIX2INT(slot->value), FIX2INT(copy->value),
                           slot->version, new_version, tx->version, txg->version);

            slot->version = new_version;
            slot->value = copy->value;
        }
    }

    tx_commit_release(tx, logs_cnt);

    return Qtrue;

  abort_and_retry:
    tx->retry_on_commit++;

    return Qfalse;
}

// tvar

static void
tvar_mark(void *ptr)
{
    struct tvar_slot *slot = (struct tvar_slot *)ptr;
    rb_gc_mark(slot->value);
}

static void
tvar_free(void *ptr)
{
    struct tvar_slot *slot = (struct tvar_slot *)ptr;
    tx_slot_lock_free(&slot->lock);
    ruby_xfree(slot);
}

static const rb_data_type_t tvar_data_type = {
    "Thread::TVar",
    {tvar_mark, tvar_free, NULL,},
    0, 0, RUBY_TYPED_FREE_IMMEDIATELY
};

static VALUE
tvar_new_(VALUE self, VALUE init)
{
    // init should be shareable
    if (RB_UNLIKELY(!rb_ractor_shareable_p(init))) {
        rb_raise(rb_eArgError, "only shareable object are allowed");
    }

    struct tx_global *txg = tx_global_ptr();
    struct tvar_slot *slot;
    VALUE obj = TypedData_Make_Struct(rb_cRactorTVar, struct tvar_slot, &tvar_data_type, slot);
    slot->version = 0;
    slot->value = init;
    slot->index = txg_next_index(txg);
    tx_slot_lock_init(&slot->lock);

    FL_SET_RAW(obj, RUBY_FL_SHAREABLE);

    return obj;
}

static VALUE
tvar_new(int argc, VALUE *argv, VALUE self)
{
    VALUE init = Qnil;
    rb_scan_args(argc, argv, "01", &init);
    return tvar_new_(self, init);
}

static VALUE
tvar_value(VALUE self)
{
    struct tx_logs *tx = tx_logs();
    struct tvar_slot *slot = DATA_PTR(self);

    if (tx->enabled) {
        return tx_get(tx, slot, self);
    }
    else {
        // TODO: warn on multi-ractors?
        return slot->value;
    }
}

static VALUE
tvar_value_set(VALUE self, VALUE val)
{
    if (RB_UNLIKELY(!rb_ractor_shareable_p(val))) {
        rb_raise(rb_eArgError, "only shareable object are allowed");
    }

    struct tx_logs *tx = tx_logs();
    tx_check(tx);
    struct tvar_slot *slot = DATA_PTR(self);
    tx_set(tx, val, slot, self);
    return val;
}

static VALUE
tvar_calc_inc(VALUE v, VALUE inc)
{
    if (RB_LIKELY(FIXNUM_P(v) && FIXNUM_P(inc))) {
        return rb_fix_plus_fix(v, inc);
    }
    else {
        return Qundef;
    }
}

static VALUE
tvar_value_increment_(VALUE self, VALUE inc)
{
    struct tx_global *txg = tx_global_ptr();
    struct tx_logs *tx = tx_logs();
    VALUE recv, ret;
    struct tvar_slot *slot = DATA_PTR(self);

    if (!tx->enabled) {
        tx_slot_lock(slot);
        {
            uint64_t new_version = txg_next_version(txg);
            recv = slot->value;
            ret = tvar_calc_inc(recv, inc);

            if (RB_LIKELY(ret != Qundef)) {
                slot->value = ret;
                slot->version = new_version;
                txg->version = new_version;
            }
        }
        tx_slot_unlock(slot);

        if (RB_UNLIKELY(ret == Qundef)) {
            // atomically{ self.value += inc }
            ret = rb_funcall(self, rb_intern("__increment__"), 1, inc);
        }
    }
    else {
        recv = tx_get(tx, slot, self);
        if (RB_UNLIKELY((ret = tvar_calc_inc(recv, inc)) == Qundef)) {
            ret = rb_funcall(recv, rb_intern("+"), 1, inc);
        }
        tx_set(tx, ret, slot, self);
    }

    return ret;
}

static VALUE
tvar_value_increment(int argc, VALUE *argv, VALUE self)
{
    switch (argc) {
      case 0: return tvar_value_increment_(self, INT2FIX(1));
      case 1: return tvar_value_increment_(self, argv[0]);
      // todo: scan args
      default: rb_raise(rb_eArgError, "2 or more arguments");
    }
}

#if 0 // unused
static struct tvar_slot *
tvar_slot_ptr(VALUE v)
{
    if (rb_typeddata_is_kind_of(v, &tvar_data_type)) {
        return DATA_PTR(v);
    }
    else {
        rb_raise(rb_eArgError, "TVar is needed");
    }
}
#endif

static VALUE
tx_atomically_body2(VALUE txptr)
{
    struct tx_logs *tx = (struct tx_logs *)txptr;

    while (1) {
        VALUE ret = rb_yield(Qnil);

        if (tx_commit(tx)) {
            return ret;
        }
        else {
            tx_reset(tx);
        }
    }
}

static VALUE
tx_atomically_rescue(VALUE txptr, VALUE err)
{
    struct tx_logs *tx = (struct tx_logs *)txptr;
    tx_reset(tx);
    return Qundef;
}

static VALUE
tx_atomically_body(VALUE txptr)
{
    VALUE ret;

    do {
        ret = rb_rescue2(tx_atomically_body2, (VALUE)txptr,
                         tx_atomically_rescue, (VALUE)txptr,
                         rb_eTxRetry, 0);
    } while (ret == Qundef);

    return ret;
}

static VALUE
tx_atomically_ensure(VALUE txptr)
{
    struct tx_logs *tx = (struct tx_logs *)txptr;
    tx_end(tx);
    return 0;
}

static VALUE
tx_atomically(VALUE self)
{
    struct tx_logs *tx = tx_begin();
    if (tx != NULL) {
        return rb_ensure(tx_atomically_body, (VALUE)tx,
                         tx_atomically_ensure, (VALUE)tx);
    }
    else {
        return rb_yield(Qnil);
    }
}

void
Init_tvar(void)
{
    rb_ext_ractor_safe(true);

    // initialixe tx_global
    struct tx_global *txg = tx_global_ptr();
    txg->slot_index = 0;
    txg->version = 0;
    rb_native_mutex_initialize(&txg->slot_index_lock);
    rb_native_mutex_initialize(&txg->version_lock);

    id_tx_logs = rb_intern("__ractor_tvar_tls__");

    // errors
    rb_eTxError = rb_define_class_under(rb_cRactor, "TransactionError", rb_eRuntimeError);
    rb_eTxRetry = rb_define_class_under(rb_cRactor, "RetryTransaction", rb_eException);
    rb_exc_tx_retry = rb_exc_new_cstr(rb_eTxRetry, "Thread::RetryTransaction");
    rb_obj_freeze(rb_exc_tx_retry);
    rb_gc_register_mark_object(rb_exc_tx_retry);

    // TxLogs
    rb_cRactorTxLogs = rb_define_class_under(rb_cRactor, "TxLogs", rb_cObject); // hidden object

    // TVar APIs
    rb_define_singleton_method(rb_cRactor, "atomically", tx_atomically, 0);

    rb_cRactorTVar = rb_define_class_under(rb_cRactor, "TVar", rb_cObject);
    rb_define_singleton_method(rb_cRactorTVar, "new", tvar_new, -1);
    rb_define_method(rb_cRactorTVar, "value", tvar_value, 0);
    rb_define_method(rb_cRactorTVar, "value=", tvar_value_set, 1);
    rb_define_method(rb_cRactorTVar, "increment", tvar_value_increment, -1);
    // rb_define_method(rb_cRactorTVar, "inspect", tvar_inspect, 0);
}
