import yope3d


class SkinVerify:
    """Reports whether a RELOADED scene restored its skinning.

    Injected into a saved copy of the smoketest scene (whose own script hosts are
    stripped first, or they would re-spawn everything on load). This is what
    proves the skin block and the .yskel sidecar actually carry a character
    across a save, rather than reloading it as a bind-pose statue.
    """

    PARAMS = {}

    def init(self, world, entity, params):
        self.t = 0.0
        self.done = False

    def update(self, world, entity, dt):
        # Wait a beat so the commit has fully finalized and a little clip time
        # has elapsed — a restored-but-not-playing instance is still a failure.
        self.t += dt
        if self.done or self.t < 1.0:
            return
        self.done = True

        e = yope3d.find_entity("SkinTestMesh")
        if e is None:
            print("[skin_verify] FAIL: skinned entity did not survive the save")
            return

        smr = yope3d.reg_get(e, "SkinnedMeshRenderer")
        if smr is None:
            print("[skin_verify] FAIL: SkinnedMeshRenderer component was not restored")
            return

        print(f"[skin_verify] entity={e.id} skeleton='{smr.skeleton}' clip='{smr.clip}' "
              f"instance={smr.instance} bones={world.bone_count(e)} "
              f"playing={world.is_skin_playing(e)} t={world.skin_time(e):.2f}")

        if smr.instance < 0:
            print("[skin_verify] FAIL: no live SkinInstance — reloaded as bind pose")
        elif world.bone_count(e) == 0:
            print("[skin_verify] FAIL: skeleton missing from the .yskel sidecar")
        elif not world.is_skin_playing(e):
            print("[skin_verify] FAIL: restored but not playing")
        else:
            print("[skin_verify] PASS: skinning survived save + reload")
