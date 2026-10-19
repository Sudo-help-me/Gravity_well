# Gravity Well

A Flipper Zero adaptation of CircularBit's browser game "The Black Hole".
It reproduces the core play loop with an independently written physics model
and original monochrome graphics; it does not bundle the browser game's code
or artwork.

## Play

- Left / Right / Up / Down: move the placement cursor
- Hold Up: cycle through moon, planet, and star
- OK: place a body at the cursor
- Hold OK: pause or resume
- Hold Left: show or hide body trails
- Hold Right: toggle dark mode
- Back: exit

The round starts with a moving planet. Bodies drift and are pulled by the
central black hole and by more massive bodies. Score accumulates while there
are multiple bodies; larger bodies and closer approaches score more. A body
captured by the hole or a collision between bodies ends the round. Press OK
to restart. The Flipper build allows up to 12 bodies at once.

Controls and physics are adapted for the Flipper's small screen, so they do
not match the browser version pixel-for-pixel.

## Build

Copy this `gravity_well` folder into the Flipper Zero firmware repository's
`applications_user` directory, then build from the firmware root:

```sh
./fbt fap_gravity_well
```

The resulting `.fap` can be copied to the device's `SD Card/apps/Games`
directory.
