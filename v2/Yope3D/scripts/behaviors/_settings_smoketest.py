"""Settings (yope3d.settings) smoketest.

Attach to any entity, press Play, and watch the console:
  - reports the resolved settings.cfg path
  - round-trips every typed accessor (str/float/int/bool) through disk:
    set -> save -> clear -> load -> get, so a pass proves the file actually
    persisted rather than the in-memory map answering its own question
  - checks the unset-key default path, has()/remove()/keys()
  - verifies malformed values fall back to the default instead of throwing
  - restores whatever keys were there before it ran (see _RESTORE), so running
    the smoketest never clobbers a real settings file

Exercises: yope3d.settings (Settings.get_*/set_*/has/remove/clear/keys/
save/load/path) and the Config overlay keys documented in the .pyi.
"""
import yope3d

# Keys this test writes. Anything pre-existing under these names is captured at
# init() and put back at the end.
_TEST_KEYS = ["_smoke_str", "_smoke_float", "_smoke_int", "_smoke_bool", "_smoke_bad"]


class SettingsSmoketest:
    def init(self, world, entity, params):
        s = yope3d.settings
        ok = True

        print(f"[settings] path={s.path}")

        # Preserve any real values living under our test keys.
        restore = {k: s.get_str(k) for k in _TEST_KEYS if s.has(k)}
        preexisting = sorted(k for k in s.keys() if k not in _TEST_KEYS)
        print(f"[settings] {len(preexisting)} pre-existing key(s): {preexisting}")

        # --- defaults for unset keys ---
        ok &= self._check("default str",   s.get_str("_nope", "fallback"), "fallback")
        ok &= self._near ("default float", s.get_float("_nope", 2.5), 2.5)
        ok &= self._check("default int",   s.get_int("_nope", 7), 7)
        ok &= self._check("default bool",  s.get_bool("_nope", True), True)
        ok &= self._check("has() unset",   s.has("_nope"), False)

        # --- typed round-trip through disk ---
        s.set_str("_smoke_str", "hello world")
        s.set_float("_smoke_float", 0.002)     # mouse-sensitivity magnitude
        s.set_int("_smoke_int", -42)
        s.set_bool("_smoke_bool", True)
        if not s.save():
            print("[settings] FAIL save() returned False")
            ok = False

        # clear() then load() — if the values come back, they came off the disk.
        s.clear()
        ok &= self._check("clear() empties", len(s.keys()), 0)
        s.load()

        ok &= self._check("str round-trip",   s.get_str("_smoke_str"), "hello world")
        ok &= self._near ("float round-trip", s.get_float("_smoke_float"), 0.002)
        ok &= self._check("int round-trip",   s.get_int("_smoke_int"), -42)
        ok &= self._check("bool round-trip",  s.get_bool("_smoke_bool"), True)

        # --- precision: >6 significant digits must survive the disk ---
        # A 6-sig-digit writer (to_string / ostream <<) mangles these: 3.14159274
        # would reload as 3.14159012, and 1234567.0 as 1234570.0.
        s.set_float("_smoke_float", 3.14159274)
        s.save(); s.clear(); s.load()
        ok &= self._near("float precision (pi)", s.get_float("_smoke_float"), 3.14159274)
        s.set_float("_smoke_float", 1234567.0)
        s.save(); s.clear(); s.load()
        ok &= self._near("float precision (1234567)", s.get_float("_smoke_float"), 1234567.0)

        # --- malformed value falls back, does not throw ---
        s.set_str("_smoke_bad", "not-a-number")
        ok &= self._near ("bad float -> default", s.get_float("_smoke_bad", 1.5), 1.5)
        ok &= self._check("bad int -> default",   s.get_int("_smoke_bad", 9), 9)

        # --- has()/remove()/keys() ---
        ok &= self._check("has() set", s.has("_smoke_str"), True)
        s.remove("_smoke_str")
        ok &= self._check("remove()", s.has("_smoke_str"), False)

        # --- restore: drop our keys, put back anything we displaced ---
        for k in _TEST_KEYS:
            s.remove(k)
        for k, v in restore.items():
            s.set_str(k, v)
        s.save()
        s.load()
        leaked = [k for k in _TEST_KEYS if s.has(k) and k not in restore]
        ok &= self._check("cleanup left no keys", leaked, [])

        print(f"[settings] {'PASS' if ok else 'FAIL'}")

    def update(self, world, entity, dt):
        pass

    def _check(self, label, got, want):
        if got == want:
            print(f"[settings]   ok   {label}: {got!r}")
            return True
        print(f"[settings]   FAIL {label}: got {got!r}, want {want!r}")
        return False

    def _near(self, label, got, want):
        # get_float returns a C++ float32, which Python widens to a float64 --
        # 0.002f reads back as 0.0020000000949949026, so an exact == against a
        # Python literal always fails. That widening is inherent to every float
        # in the yope3d API (Transform.position and friends behave the same
        # way), not a persistence error: the value on disk is bit-exact. Compare
        # at float32 resolution instead.
        #
        # The tolerance is float32 epsilon (~1.19e-7), not a looser round number:
        # it has to be tight enough to still catch a genuinely lossy writer. A
        # 6-sig-digit save turns 3.14159274 into 3.14159012 -- only 8.3e-7 off,
        # which a 1e-6 tolerance would wave through.
        if abs(got - want) <= 1.2e-7 * max(1.0, abs(want)):
            print(f"[settings]   ok   {label}: {got!r} ~= {want!r}")
            return True
        print(f"[settings]   FAIL {label}: got {got!r}, want ~{want!r}")
        return False
