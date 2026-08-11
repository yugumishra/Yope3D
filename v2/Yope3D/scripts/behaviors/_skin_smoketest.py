import yope3d


class SkinSmoketest:
    """Spawns the 2-bone skinTest model over a ground plane, lit by a downward
    spot marked as the scene shadow caster.

    The shadow is the point of the setup, not decoration: Renderer::recordSkinningPass
    dispatches the compute skinning BEFORE recordShadowPass, because the shadow
    pass — not the main pass — is the first consumer of the skinned vertex buffer.
    If that barrier or its ordering were wrong, the column would bend while its
    shadow stayed rigid. Watch the shadow bend in lockstep with the mesh.

    Not a Catch2 test — it needs a GPU and a real frame loop.
    """

    PARAMS = {}

    def init(self, world, entity, params):
        print("[skin_smoketest] init")
        self.t = 0.0
        self.reported = False
        self.saved = False

        # Ground for the shadow to land on, top surface flush with y=0 (the
        # model's feet). add_static_aabb is COLLISION ONLY — every add_* physics
        # helper is mesh-less by design — so the visible slab has to be attached
        # separately or the floor is invisibly there and catches no visible shadow.
        self.ground = world.add_static_aabb(yope3d.Vec3(0.0, -0.25, 0.0),
                                            yope3d.Vec3(12.0, 0.25, 12.0))
        world.attach_box_mesh(self.ground, yope3d.Vec3(12.0, 0.25, 12.0),
                              0.62, 0.63, 0.66)

        # Spot light above and in front, angled down at the column. A spot gets a
        # tight perspective shadow frustum (2D shadow map), which resolves a small
        # object far better than the directional ortho box would.
        self.light = world.add_point_light(
            pos=yope3d.Vec3(2.5, 6.0, 3.0),
            color=yope3d.Vec3(1.0, 0.96, 0.9),
            intensity=60.0,
        )
        ls = yope3d.reg_get(self.light, "LightSource")
        if ls is not None:
            ls.type = 2                                    # spot
            ls.direction = yope3d.Vec3(-0.35, -0.85, -0.42)  # aims at ~(0,1,0)
            ls.outer_cone_angle = 0.65
            ls.inner_cone_angle = 0.35
            ls.linear = 0.0
            ls.quadratic = 0.0
        world.set_shadow_caster(self.light)
        print("[skin_smoketest] ground + spot shadow caster placed")

        self.entities = world.add_model("models/skinTest.gltf")
        print(f"[skin_smoketest] imported {len(self.entities)} entities")

        self.skinned = None
        for e in self.entities:
            smr = yope3d.reg_get(e, "SkinnedMeshRenderer")
            if smr is not None:
                self.skinned = e
                print(f"[skin_smoketest]   skinned entity={e.id} skeleton='{smr.skeleton}' "
                      f"clip='{smr.clip}' instance={smr.instance} mode={smr.mode}")

        if self.skinned is None:
            print("[skin_smoketest] FAIL: no SkinnedMeshRenderer was attached")
            return

        tf = yope3d.reg_get(self.skinned, "Transform")
        if tf is not None:
            tf.position = yope3d.Vec3(0.0, 0.0, 0.0)

        # Bone count + name lookup go through the same path attach_to_bone uses.
        print(f"[skin_smoketest] bones={world.bone_count(self.skinned)} "
              f"bone0='{world.bone_name(self.skinned, 0)}' "
              f"bone1='{world.bone_name(self.skinned, 1)}' "
              f"index('bone1')={world.bone_index(self.skinned, 'bone1')}")

        # Socket test: pin a small cube to the upper bone. It should ride the
        # bend, tracing an arc rather than sitting still.
        self.prop = world.add_static_aabb(yope3d.Vec3(0.0, 0.0, 0.0),
                                          yope3d.Vec3(0.12, 0.12, 0.12))
        world.attach_box_mesh(self.prop, yope3d.Vec3(0.12, 0.12, 0.12),
                              0.2, 0.55, 0.95)
        ok = world.attach_to_bone(self.prop, self.skinned, "bone1")
        print(f"[skin_smoketest] attach_to_bone(prop -> bone1) = {ok}")

    def update(self, world, entity, dt):
        self.t += dt

        # Save/reload round-trip check (opt-in via YOPE_SKIN_SAVE). Proves the
        # .ymesh v2 skin block and the .yskel sidecar actually carry a character
        # across a save — the thing that used to reload as a bind-pose statue.
        if not self.saved and self.t > 1.5:
            self.saved = True
            import os
            if os.environ.get("YOPE_SKIN_SAVE"):
                ok = world.save_scene("scenes/_skin_roundtrip.yscene")
                print(f"[skin_smoketest] save_scene -> {ok}")

        if not self.reported and self.t > 3.0:
            self.reported = True
            playing = world.is_skin_playing(self.skinned) if self.skinned else False
            print(f"[skin_smoketest] t={self.t:.1f} playing={playing} "
                  f"skin_time={world.skin_time(self.skinned):.2f}" if self.skinned else "")
