# Profile icon attribution

The account profile selector uses six Fallout 4 perk images:

- Armorer
- Hacker
- Rifleman
- Medic
- Scrapper
- Cap Collector

Source repository:

- https://github.com/CircuitBread0111/Fallout_Perk_Planner
- pinned source commit: `918547cc872c3288122f9d15ed0416cf33aa8bbf`
- source files: `perk_images/*.png`

`scripts/fetch-profile-icons.js` downloads the six images during the static build, verifies each download against its exact Git blob SHA, and places the verified PNGs in `assets/profile-icons/` for local deployment. The source binaries are intentionally not committed to this repository and users cannot upload replacement profile images.

The selector also offers four Fallout 4 faction icons committed to this repository:

- Brotherhood of Steel (`assets/profile-images/Icon__Brotherhood.png`)
- Institute (`assets/profile-images/Icon__Institute.png`)
- Minutemen (`assets/profile-images/Icon__Minutemen.png`)
- Railroad (`assets/profile-images/Icon__Railroad.png`)

These committed images are copied into the static build by `scripts/build-static.js` and served from `/assets/profile-images/`.

The Fallout 4 artwork, Vault Boy imagery, and faction emblems remain the property of Bethesda Softworks / ZeniMax Media. Commonwealth Online does not claim ownership of that artwork and is not affiliated with or endorsed by Bethesda Softworks or ZeniMax Media.
