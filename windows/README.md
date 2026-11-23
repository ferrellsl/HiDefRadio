# OpenDial for Windows

Hybrid digital radio (NRSC-5) for an RTL-SDR stick, for Windows 10 and 11: hybrid digital
FM/AM with traffic and weather maps, no internet needed. It is the Windows version of
OpenDial (formerly HiDef Radio Maps), which is based on HiDef Radio, an Android app by
derek20la, and adds traffic and weather maps, date and time stamps on the maps, ad image
blocking, big album art and a signal-details pane. Version 2.0 installs over HiDef Radio
for Windows and keeps your settings and favorites.

## What it does

- Tunes FM (87.75-107.9 MHz) and AM (530-1700 kHz) and plays HD1-HD8, with analog-to-HD blend
  ("Auto"), as on Android.
- Shows the station name, slogan, song, album art and station logo.
- Shows traffic and weather maps for stations that send them (TTN and HERE), on the same
  OpenStreetMap base map as the Android fork. The radar loops through up to 12 frames.
  Double-click a map to fill the window. Esc or another double-click brings the window back.
- Stamps each map with the date and time the station made it, in your own time zone.
- Fits small screens: on a short window, such as an 8" 1280x800 or a 1280x720 tablet, the
  album art moves beside the song and artist, as big as the window allows, the signal line
  moves to the top of the signal details, and the layout tightens up (since 1.0.3; 1.0.4
  makes the art bigger).
- Big album art: double-click (or double-tap) the album art, and it fills the left column.
  The station, program, song and artist move into one line above it, which scrolls when it
  is too long, and the favorites hide meanwhile. Double-click the picture or that line to go
  back. The app remembers the choice (since 1.0.5).
- Blocks ad pictures: right-click the album art and choose "Block this picture". Settings
  clears the list.
- Signal details: MER, BER, sync, gain, analog quieting, RDS, blend and alignment, and the
  station's FCC record, and the OpenStreetMap credit for the maps. Click the signal line or the "Signal details" heading to show or
  hide them. When they are hidden, the maps take their place.
- Favorites: click the star next to the station name. Right-click a favorite to move,
  rename or delete it.
- Recording: click ⏺ (or press R) to record, and again to stop. Settings chooses what it
  records: the audio you hear (AAC in .m4a, about 1 MB a minute), the raw IQ samples (.cu8,
  8-bit I/Q at 1,488,375 samples a second, about 180 MB a minute, which nrsc5 plays back with
  `-r` and most SDR programs open), or both. IQ stops by itself after the limit you set
  (60 s unless you choose otherwise). Changing station or stopping the radio also ends a
  recording and saves it. Files go to Music\OpenDial (or Music\HiDef Radio, if you recorded there before 2.0) unless you pick another folder.

- HD1-HD8: when a station has more than one program, a button shows the one you hear
  ("HD1 ❯"). Click it and a strip with all of them opens across the window, each with
  its name; click one to switch. Clicking anywhere else, Esc, or a few seconds untouched
  closes it (since 1.0.6).

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

    powershell -ExecutionPolicy Bypass -File package.ps1 -Version 2.0.0

That builds `build\OpenDial.exe` (one static exe, no DLLs) and its `data` folder (the FCC
station list, the base map, the DSEG7 font and the licenses, copied from the Android app),
then puts a zip and an Inno Setup installer in `dist`. Needs MSYS2 packages
mingw-w64-x86_64-gcc, mingw-w64-x86_64-cmake and mingw-w64-x86_64-ninja, and Inno Setup 6.

For older and 32-bit PCs there is a 32-bit build, made the same way with `-Arch x86`:

    powershell -ExecutionPolicy Bypass -File package.ps1 -Version 2.0.0 -Arch x86

It builds in `build32` and names its files `OpenDial-Windows-<version>-x86`. It needs the
32-bit MSYS2 packages mingw-w64-i686-gcc, mingw-w64-i686-cmake and mingw-w64-i686-ninja.
Like the 64-bit build it needs a CPU with SSE2, SSE3 and SSSE3 (Intel Core 2, Atom or later;
AMD Bobcat, Bulldozer or later).
If the 32-bit gcc stops without a message (error 0xC0000139), an old program has put its own
libgcc_s_dw2-1.dll or libwinpthread-1.dll in Windows\SysWOW64, which Windows loads first:
copy MSYS2's two from mingw32\bin into mingw32\lib\gcc\i686-w64-mingw32\<version>.

Settings, favorites and blocked pictures are kept in `%APPDATA%\HiDefRadio` (the name from before 2.0, kept so updates keep them). Saved logos,
radar frames and the log file (`hidefradio.log`) are kept in `%LOCALAPPDATA%\HiDefRadio`.

## License

GPL-3.0-or-later, like HiDef Radio. See `data/licenses/NOTICES.txt` for the libraries, the
font and the data. Map data © OpenStreetMap contributors (CC BY-SA).
"HD Radio" is a trademark of iBiquity Digital Corporation, an Xperi company. This is an
independent hobby project, not affiliated with or endorsed by Xperi or iBiquity.
