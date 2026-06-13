"""Tiny in-process event bus for inter-behavior communication (pure Python, no C++).

A blessed support module so behaviors can talk to each other without each one
growing its own callback list. Import and use from any behavior:

    from behaviors import _events

    # publisher
    _events.emit("enemy_died", enemy_id, points=50)

    # subscriber (e.g. in init)
    def _on_death(enemy_id, points=0):
        self.score += points
    _events.subscribe("enemy_died", _on_death)
    # ... and unsubscribe in on_unload to avoid stale callbacks:
    _events.unsubscribe("enemy_died", _on_death)

Handlers run synchronously on the calling thread (the main/script thread). An
exception in one handler is printed and does not stop the others.
"""
from collections import defaultdict

_subs = defaultdict(list)


def subscribe(name, fn):
    """Register `fn` to be called on every emit(name, ...)."""
    _subs[name].append(fn)


def unsubscribe(name, fn):
    """Remove a previously-subscribed handler (no-op if absent)."""
    try:
        _subs[name].remove(fn)
    except ValueError:
        pass


def emit(name, *args, **kwargs):
    """Call every handler registered for `name`. Returns the number invoked."""
    handlers = list(_subs.get(name, ()))  # copy so handlers may (un)subscribe
    for fn in handlers:
        try:
            fn(*args, **kwargs)
        except Exception as e:  # noqa: BLE001 — one bad handler shouldn't break the rest
            print(f"[_events] handler for '{name}' raised: {e}")
    return len(handlers)


def clear(name=None):
    """Drop all handlers for `name`, or every handler if `name` is None."""
    if name is None:
        _subs.clear()
    else:
        _subs.pop(name, None)
