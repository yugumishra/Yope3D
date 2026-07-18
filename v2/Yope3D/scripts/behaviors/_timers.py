"""Timers + coroutine scheduling for behaviors (pure Python, no C++).

Blessed support module so every behavior doesn't reinvent a dt accumulator.
You drive it once per frame from your behavior's update() by calling tick(dt).

One-shot / repeating callbacks:

    from behaviors import _timers

    def update(self, world, entity, dt):
        _timers.tick(dt)              # advance all scheduled work
        ...
    # elsewhere:
    _timers.after(2.0, lambda: print("2s later"))
    handle = _timers.every(0.5, spawn_wave)   # repeating
    _timers.cancel(handle)

Generator coroutines (yield a number of seconds to wait):

    def patrol():
        while True:
            go_left()
            yield 1.5            # wait 1.5s
            go_right()
            yield 1.5
    _timers.start(patrol())

All callbacks run on the main/script thread inside tick(). `tick` should be
called from exactly one place per frame (e.g. a single manager behavior) so
timers advance once per frame.
"""
import itertools

_next_id = itertools.count(1)
_timers = {}       # id -> {"left": float, "interval": float|None, "fn": callable}
_coros = []        # list of [generator, time_left]


def after(seconds, fn):
    """Call `fn()` once after `seconds`. Returns a handle for cancel()."""
    tid = next(_next_id)
    _timers[tid] = {"left": seconds, "interval": None, "fn": fn}
    return tid


def every(seconds, fn):
    """Call `fn()` every `seconds` until cancelled. Returns a handle."""
    tid = next(_next_id)
    _timers[tid] = {"left": seconds, "interval": seconds, "fn": fn}
    return tid


def cancel(handle):
    """Cancel an after()/every() timer (no-op if already fired/cancelled)."""
    _timers.pop(handle, None)


def start(generator):
    """Drive a generator coroutine that `yield`s seconds-to-wait between steps."""
    _coros.append([generator, 0.0])


def tick(dt):
    """Advance all timers and coroutines by `dt`. Call once per frame."""
    # after/every timers
    for tid in list(_timers.keys()):
        t = _timers.get(tid)
        if t is None:
            continue
        t["left"] -= dt
        if t["left"] <= 0.0:
            try:
                t["fn"]()
            except Exception as e:  # noqa: BLE001
                print(f"[_timers] callback raised: {e}")
            if t["interval"] is not None:
                t["left"] += t["interval"]   # reschedule (carry overshoot)
            else:
                _timers.pop(tid, None)

    # generator coroutines
    for item in list(_coros):
        item[1] -= dt
        if item[1] <= 0.0:
            try:
                wait = next(item[0])
                item[1] = float(wait) if wait else 0.0
            except StopIteration:
                _coros.remove(item)
            except Exception as e:  # noqa: BLE001
                print(f"[_timers] coroutine raised: {e}")
                _coros.remove(item)


def clear():
    """Drop all pending timers and coroutines."""
    _timers.clear()
    _coros.clear()
