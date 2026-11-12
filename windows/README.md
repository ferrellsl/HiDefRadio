# HiDef Radio for Windows

An HD Radio (NRSC-5) receiver for an RTL-SDR stick, for Windows 10 and 11. It is the Windows
version of HiDef Radio, an Android app by derek20la, with the additions from the
ferrellsl/HiDefRadio fork: traffic and weather maps, date and time stamps on the maps, ad
image blocking and a signal-details pane.

## What it does

- Tunes FM (87.75-107.9 MHz) and AM (530-1700 kHz) and plays HD1-HD8, with analog-to-HD blend
  ("Auto"), as on Android.
- Shows the station name, slogan, song, album art and station logo.
- Shows traffic and weather maps for stations that send them (TTN and HERE), on the same
  OpenStreetMap base map as the Android fork. The radar loops through up to 12 frames.
  Double-click a map to fill the window. Esc or another double-click brings the window back.
- Stamps each map with the date and time the station made it, in your own time zone.
- Blocks ad pictures: right-click the album art and choose "Block this picture". Settings
  clears the list.
- Signal details: MER, BER, sync, gain, analog quieting, RDS, blend and alignment, and the
  station's FCC record. Click the signal line or the "Signal details" heading to show or
  hide them. When they are hidden, the maps take their place.
- Favorites: click the star next to the station name. Right-click a favorite to move,
  rename or delete it.
- Recording: click ⏺ (or press R) to record, and again to stop. Settings chooses what it
  records: the audio you hear (AAC in .m4a, about 1 MB a minute), the raw IQ samples (.cu8,
  8-bit I/Q at 1,488,375 samples a second, about 180 MB a minute, which nrsc5 plays back with
  `-r` and most SDR programs open), or both. IQ stops by itself after the limit you set
  (60 s unless you choose otherwise). Changing station or stopping the radio also ends a
  recording and saves it. Files go to Music\HiDef Radio unless you pick another folder.

Keys: Left/Right step the frequency; T types one; 1-8 pick HD1-HD8; Space pauses;
R records; F adds or removes a favorite; D shows or hides the details; F11 fills the screen.
The mouse wheel over the frequency display tunes.

## Hardware

Any RTL2832U dongle (NESDR, RTL-SDR Blog V3/V4 and others). Windows needs the WinUSB
driver on it. Install it once with [Zadig](https://zadig.akeo.ie): Options > List All Devices,
pick "Bulk-In, Interface (Interface 0)" or "RTL2838UHIDIR", choose WinUSB, and click
Replace Driver.

## How it is built

The radio engine is the Android app's own native code (`../app/src/main/cpp`: native-lib.cpp,
the FM/AM demodulators, blend, RDS, and nrsc5 3.2.0, FAAD2, FFTW, librtlsdr and libusb),
compiled unchanged for Windows. A small compatibility layer stands in for the Android parts
(`compat/`): a minimal JNI, Android logging written to a log file, and POSIX sockets mapped to
Winsock. The dongle is opened by its number instead of an Android USB file descriptor.

The window, audio (WASAPI), maps (WIC) and settings are new C++ code in `src/`, ported from
the Kotlin app (RadioService, MapFeed, ArtBlock, LogoCache, GainKeeper, Stations, Signal).

Build with MSYS2's MinGW-w64 gcc, CMake and Ninja (nrsc5 uses C99 complex numbers, which
MSVC can't compile). From this `windows` folder:

    powershell -ExecutionPolicy Bypass -File package.ps1 -Version 1.0.1

That builds `build\HiDefRadio.exe` (one static exe, no DLLs) and its `data` folder (the FCC
station list, the base map, the DSEG7 font and the licenses, copied from the Android app),
then puts a zip and an Inno Setup installer in `dist`. Needs MSYS2 packages
mingw-w64-x86_64-gcc, mingw-w64-x86_64-cmake and mingw-w64-x86_64-ninja, and Inno Setup 6.

Settings, favorites and blocked pictures are kept in `%APPDATA%\HiDefRadio`. Saved logos,
radar frames and the log file (`hidefradio.log`) are kept in `%LOCALAPPDATA%\HiDefRadio`.

## License

GPL-3.0-or-later, like HiDef Radio. See `data/licenses/NOTICES.txt` for the libraries, the
font and the data. Map data © OpenStreetMap contributors (CC BY-SA).
"HD Radio" is a trademark of iBiquity Digital Corporation, an Xperi company. This is an
independent hobby project, not affiliated with or endorsed by Xperi or iBiquity.
