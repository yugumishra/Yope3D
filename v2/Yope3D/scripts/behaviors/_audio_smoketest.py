import yope3d
from yope3d import audio


class AudioSmoketest:
    """Scene-logic host script exercising play_music (stream + non-stream),
    mixer buses, and gain fades — for manual verification of the §8 audio
    improvements. Not a Catch2 test (needs real OpenAL playback); watch the
    printed timeline and confirm no exceptions land in the Console."""

    PARAMS = {}

    def init(self, world, entity, params):
        print("[audio_smoketest] init")
        self.t = 0.0
        self.phase = 0

        # Streaming music, looping, 2s fade-in.
        self.music = yope3d.play_music("audios/test.ogg", loop=True, fade_in=2.0, stream=True)
        print(f"[audio_smoketest] streaming source created: {self.music is not None}")

        # A concurrent SFX one-shot on the default SFX bus.
        self.sfx = yope3d.play_sound("audios/flashlight-click-on.ogg", gain=1.0)
        print(f"[audio_smoketest] sfx source created: {self.sfx is not None}")

    def update(self, world, entity, dt):
        self.t += dt
        if self.music is None:
            return

        if self.phase == 0 and self.t > 3.0:
            print(f"[audio_smoketest] t={self.t:.1f} music.is_playing={self.music.is_playing()} (expect True, streamed past initial buffers)")
            self.phase = 1

        if self.phase == 1 and self.t > 4.0:
            print("[audio_smoketest] muting music bus (sfx should be unaffected)")
            audio.set_bus_gain(yope3d.BUS_MUSIC, 0.0)
            self.phase = 2

        if self.phase == 2 and self.t > 6.0:
            print("[audio_smoketest] restoring music bus + fading out over 2s")
            audio.set_bus_gain(yope3d.BUS_MUSIC, 1.0)
            audio.fade_gain(self.music, 0.0, 2.0)
            self.phase = 3

        if self.phase == 3 and self.t > 9.0:
            print(f"[audio_smoketest] t={self.t:.1f} final music.is_playing={self.music.is_playing()}")
            self.phase = 4
