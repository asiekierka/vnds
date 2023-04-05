# VNDS

Personal attempt at updating the legendary VNDS, as part of a bored weekend project.

## Changelog

Changes so far:

* Full DSi mode support.
* Updated dependencies:
    * FreeType: 2.3.5 -> 2.13.0
    * libpng: 1.2.8 -> 1.6.39
    * zlib: 1.2.3 -> 1.2.13
* Removed non-GPL-compliant MP3/AAC decoders. Instead, the recommended audio format is the limited-hardware-friendly [WavPack](https://www.wavpack.com/).
    * .mp3, .aac, etc. file extensions will automatically be replaced by .wv, so editing scripts is not necessary.
    * This might allow for higher-quality music playback than previously available? I haven't benchmarked it too much, and there is room for further optimization.
* Minor cleanup/optimizations.

# Previous README

Note that anonNL indicates that jake's contributions starting with 1.5.0 (lost) are a separate branch based on 1.4.7 (lost)
so these commits are actually older and precede the true latest version 1.4.9 which merges changes from jake's 1.5.3 and 
provides newer patches, confusingly. (as shown by the crossed out links and the 1.4.9 ChangeLog) https://web.archive.org/web/20140218222855/digital-haze.net/projects/vnds.html

Additional documentation is available in the manual folder

### Installation

1. Extract the .tar.gz onto the root of the SD card so the path is /vnds/.
2. To install a visual novel, extract the tar.gz in /vnds/novels, so it has its own folder (ie. /vnds/novels/tsukihime).
3. Sound files should end up in /vnds/novels/*game folder*/sound/.

#### NOTICE:

* any save prior to version 1.3.1 needs to be ran though the sav2xml.py utility in the tools/ folder. requires python(http://python.org) to run
* alternatively: http://digital-haze.net/files/vnds/fixsave.php

### Running

Should be easy enough

### Troubleshooting

* Make sure the path is correct
* If you run it and you get stuck at a white screen, launch it with an alternative launcher, such as dsorganize or dschannels.

### Contact

* Forum: http://digital-haze.net/ch/vnds
* IRC:   irc.rizon.net #vnds

### Credits

* Programming: Jake Probst, anoNL

