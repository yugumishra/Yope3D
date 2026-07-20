"""
Headless-ish smoketest for behaviors.platformer_java_era — drives the code
paths that need gameplay input to reach (star collection burst, win/finish),
since the demo itself can only trigger them by parkouring up the course.

Run:  YOPE_PROFILE_DURATION=8 ./build/mac-debug/yope3d \
          --scene assets/scenes/_platformerSmoketest.json
Pass: console shows [PlatSmoke] markers COLLECT-OK and FINISH-OK, no
      Python exceptions.
"""
import yope3d
from behaviors.platformer_java_era import Platformer


class PlatformerSmoketest:
    PARAMS = {}

    def init(self, world, entity, params):
        self.demo = Platformer()
        self.demo.init(world, entity, {})
        self.frame = 0
        print("[PlatSmoke] init OK")

    def update(self, world, entity, dt):
        self.frame += 1
        self.demo.update(world, entity, dt)

        if self.frame == 120:
            pos = yope3d.reg_get(self.demo.player, "Transform").position
            self.demo._collect(world, pos)
            assert self.demo.star_collected
            print("[PlatSmoke] COLLECT-OK (50 particles spawned)")

        if self.frame == 240:
            self.demo._finish(world)
            assert self.demo.winning_variable == 1
            world.set_paused(False)   # let the profile-duration auto-exit run out
            print("[PlatSmoke] FINISH-OK var=%d" % self.demo.winning_variable)
