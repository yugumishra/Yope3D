"""Composite-behaviors smoketest (composite-behaviors.md).

One process, one scene, exercising the whole stack feature end to end. The scene
authors three entities:

  * "Stacked"  -> a two-behavior stack (Health + Burnable) via the "scripts"
                  array, i.e. a CompositeScript under the hood.
  * "Single"   -> one Health behavior (legacy singular form), for the runtime
                  auto-promotion test.
  * "Coord"    -> this Coordinator, which runs the assertions once on frame 1.

What it proves (watch the console for [composite] lines):

  1. Per-child params: the stack's Health.hp and Burnable.rate came from each
     child's own params block, not a shared blob.
  2. get_behavior identity: get_behavior(e, "Health") / (e, "Burnable") resolve
     by class across the stack; the 1-arg get_behavior(stacked) raises (ambiguous);
     the composite host is never returned.
  3. Runtime auto-promotion: attach_script() a second behavior onto the Single
     entity -> it transparently becomes a stack, the original Health keeps its
     live state (not re-init'd), and both are now reachable by class.
  4. Duplicate rejection: attaching a class the entity already carries returns
     False.
  5. Save-state fan-out: save_game() writes the stack's runtime state as a
     class-keyed object {"Health": {...}, "Burnable": {...}} on the entity node.
"""
import json
import os
import yope3d

_STATE = {"ran": False}


class Health:
    PARAMS = {
        "hp":       {"type": "int",   "default": 100, "label": "Max HP"},
        "regen":    {"type": "float", "default": 1.0, "label": "Regen /s"},
    }

    def init(self, world, entity, params):
        self.hp = params.get("hp", 100)
        self.regen = params.get("regen", 1.0)
        self.damage_taken = 0

    def update(self, world, entity, dt):
        pass

    def take_damage(self, n):
        self.hp -= n
        self.damage_taken += n

    def save_state(self):
        return {"hp": self.hp, "damage_taken": self.damage_taken}

    def load_state(self, s):
        self.hp = s.get("hp", self.hp)
        self.damage_taken = s.get("damage_taken", 0)


class Burnable:
    PARAMS = {
        "rate": {"type": "float", "default": 5.0, "label": "Burn Rate"},
    }

    def init(self, world, entity, params):
        self.rate = params.get("rate", 5.0)
        self.burning = False

    def update(self, world, entity, dt):
        pass

    def ignite(self):
        self.burning = True

    def save_state(self):
        return {"burning": self.burning}

    def load_state(self, s):
        self.burning = s.get("burning", False)


class CompositeSmoketest:
    def init(self, world, entity, params):
        self._done = False

    def update(self, world, entity, dt):
        if self._done or _STATE["ran"]:
            return
        self._done = True
        _STATE["ran"] = True
        self._run(world)

    # ---------------------------------------------------------------------
    def _run(self, world):
        ok = True
        stacked = yope3d.find_entity("Stacked")
        single = yope3d.find_entity("Single")
        ok &= self._c("found Stacked", stacked is not None, True)
        ok &= self._c("found Single", single is not None, True)
        if stacked is None or single is None:
            print("[composite] FAIL missing entities"); return

        # 1. Per-child params on the stack.
        h = yope3d.get_behavior(stacked, "Health")
        b = yope3d.get_behavior(stacked, "Burnable")
        ok &= self._c("stack Health resolved", h is not None, True)
        ok &= self._c("stack Burnable resolved", b is not None, True)
        if h: ok &= self._c("stack Health.hp", h.hp, 120)
        if b: ok &= self._c("stack Burnable.rate", b.rate, 7.0)

        # 2. 1-arg on a stack is ambiguous -> raises.
        raised = False
        try:
            yope3d.get_behavior(stacked)
        except Exception:
            raised = True
        ok &= self._c("1-arg get_behavior(stack) raises", raised, True)

        # 3. Single entity: 1-arg works, class form works.
        sh = yope3d.get_behavior(single)
        ok &= self._c("single 1-arg resolves", sh is not None, True)
        if sh:
            ok &= self._c("single Health.hp", sh.hp, 50)
            sh.take_damage(10)          # mutate live state before promotion

        # 4. Runtime auto-promotion: attach a second behavior.
        attached = yope3d.attach_script(single, "behaviors._composite_smoketest",
                                        "Burnable", {"rate": 3.0})
        ok &= self._c("attach_script promotes", attached, True)
        # Original Health state preserved (not re-init'd): hp still 40 after damage.
        sh2 = yope3d.get_behavior(single, "Health")
        ok &= self._c("promoted Health preserved state", sh2.hp if sh2 else None, 40)
        sb = yope3d.get_behavior(single, "Burnable")
        ok &= self._c("promoted Burnable reachable", sb.rate if sb else None, 3.0)
        # Now 1-arg on the promoted entity is ambiguous too.
        raised2 = False
        try:
            yope3d.get_behavior(single)
        except Exception:
            raised2 = True
        ok &= self._c("1-arg on promoted raises", raised2, True)

        # 5. Duplicate rejection.
        dup = yope3d.attach_script(single, "behaviors._composite_smoketest",
                                   "Health", {})
        ok &= self._c("duplicate attach rejected", dup, False)

        # 6. Save-state fan-out: class-keyed scriptState for the stack.
        ok &= self._verify_save()

        print(f"[composite] {'ALL PASS' if ok else 'FAILURES ABOVE'}")

    def _verify_save(self):
        name = "smoke/composite_smoketest.ysave"
        if not yope3d.save_game(name):
            return self._c("save_game", False, True)
        path = yope3d.save_path(name)
        ok = True
        try:
            with open(path) as f:
                doc = json.load(f)
            states = {}
            for e in doc.get("entities", []):
                if "scriptState" in e:
                    states[e.get("Name", {}).get("value", "?")] = json.loads(e["scriptState"])
            stacked_state = states.get("Stacked", {})
            ok &= self._c("Stacked scriptState is class-keyed",
                          set(stacked_state.keys()) == {"Health", "Burnable"}, True)
            ok &= self._c("Stacked Health.hp in save", stacked_state.get("Health", {}).get("hp"), 120)
        finally:
            try:
                os.remove(path)
                binp = path.rsplit(".", 1)[0] + ".meshbin"
                if os.path.exists(binp):
                    os.remove(binp)
            except OSError:
                pass
        return ok

    def _c(self, label, got, want):
        if got == want:
            print(f"[composite]   ok   {label}: {got!r}")
            return True
        print(f"[composite]   FAIL {label}: got {got!r}, want {want!r}")
        return False
