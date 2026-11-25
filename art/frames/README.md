# MONODRONE scene strip: frames for editing

Each scene has a folder `NN_NAME_place`. Everything is 240 x 88, 1-bit, **white = ink** (as on the screen, light on dark).

- `base.png`: the place as drawn when the scene is chosen. The motion is drawn over it, live.
- `mask.png`: where something stands (white). Cloud and smoke pass behind it, and BURN's flash only lights the sky (black).
- `frame_00.png` to `frame_47.png`: about 2.3 s of the motion as the firmware draws it, a frame every ~48 ms, at mid level with no wind, with a Geiger spark now and then. It never loops on the device: ash, smoke and sparks are random each time, so these are a sample.
- `preview.gif`: the frames at 2x.

Scenes that share a place (e.g. 1, 7, 9 and 13 are all REACTOR) differ a little, because each scene's number seeds its debris, trees, graffiti and so on.

## Sending edits back

Put edited files back in the same folders with the same names, keeping 240 x 88, black and white only.
- **Edited `base.png` (and `mask.png`)**: the firmware uses your picture instead of drawing the place. The cloud, smoke, ash and sparks still move over it.
- **Edited frames**: the firmware plays your frames as a loop instead (any number; about 2.6 KB each). Say which scenes and whether the live ash and sparks should still go over them.

Made by `tools/land_frames.c`.
