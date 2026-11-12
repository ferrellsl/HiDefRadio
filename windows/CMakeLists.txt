cmake_minimum_required(VERSION 3.22)
project(HiDefRadioWin C CXX)

# The Android app's native sources are shared, not copied.
set(HIDEF_CPP "${CMAKE_SOURCE_DIR}/../app/src/main/cpp" CACHE PATH "The Android app's native sources (app/src/main/cpp)")
set(EXT ${HIDEF_CPP}/external)

set(CMAKE_C_FLAGS_RELEASE "-O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# ---- libusb (Windows backend: WinUSB) ----
add_library(usb-1.0 STATIC
    ${EXT}/libusb/libusb/core.c ${EXT}/libusb/libusb/descriptor.c ${EXT}/libusb/libusb/hotplug.c
    ${EXT}/libusb/libusb/io.c ${EXT}/libusb/libusb/strerror.c ${EXT}/libusb/libusb/sync.c
    ${EXT}/libusb/libusb/os/events_windows.c ${EXT}/libusb/libusb/os/threads_windows.c
    ${EXT}/libusb/libusb/os/windows_common.c ${EXT}/libusb/libusb/os/windows_usbdk.c
    ${EXT}/libusb/libusb/os/windows_winusb.c)
target_include_directories(usb-1.0 PRIVATE ${CMAKE_SOURCE_DIR}/compat/libusb ${EXT}/libusb/libusb/os)
target_include_directories(usb-1.0 PUBLIC ${EXT}/libusb/libusb)

# ---- librtlsdr (RTL-SDR Blog fork, as in the app) ----
add_library(rtlsdr STATIC
    ${EXT}/rtl-sdr-blog/src/librtlsdr.c ${EXT}/rtl-sdr-blog/src/tuner_e4k.c
    ${EXT}/rtl-sdr-blog/src/tuner_fc0012.c ${EXT}/rtl-sdr-blog/src/tuner_fc0013.c
    ${EXT}/rtl-sdr-blog/src/tuner_fc2580.c ${EXT}/rtl-sdr-blog/src/tuner_r82xx.c)
target_include_directories(rtlsdr PUBLIC ${EXT}/rtl-sdr-blog/include)
target_compile_definitions(rtlsdr PUBLIC rtlsdr_STATIC)
target_link_libraries(rtlsdr PUBLIC usb-1.0)

# ---- FFTW 3.3.10 single precision, SSE2/AVX/AVX2 codelets (picked at run time) ----
set(ENABLE_FLOAT ON CACHE BOOL "" FORCE)
set(ENABLE_SSE2 ON CACHE BOOL "" FORCE)
set(ENABLE_AVX ON CACHE BOOL "" FORCE)
set(ENABLE_AVX2 ON CACHE BOOL "" FORCE)

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
add_subdirectory(${EXT}/fftw fftw EXCLUDE_FROM_ALL)
target_include_directories(fftw3f PUBLIC $<BUILD_INTERFACE:${EXT}/fftw/api>)
target_compile_definitions(fftw3f PRIVATE WITH_OUR_MALLOC)

# ---- FAAD2 with nrsc5's HDC patch ----
set(FAAD_BUILD_CLI OFF CACHE BOOL "" FORCE)
add_subdirectory(${EXT}/faad2 faad2 EXCLUDE_FROM_ALL)

# ---- nrsc5 3.2.0 (library only) ----
set(N ${EXT}/nrsc5/src)
add_library(nrsc5 STATIC
    ${N}/acquire.c ${N}/sync.c ${N}/decode.c ${N}/frame.c ${N}/output.c ${N}/pids.c
    ${N}/here_images.c ${N}/input.c ${N}/nrsc5.c ${N}/rtltcp.c ${N}/firdecim_q15.c
    ${N}/conv_dec.c ${N}/rs_init.c ${N}/rs_decode.c ${N}/unicode.c ${N}/strndup.c)
set_target_properties(nrsc5 PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_compile_definitions(nrsc5 PRIVATE _GNU_SOURCE HAVE_FAAD2 HAVE_SSE2 HAVE_SSE3)
target_compile_options(nrsc5 PRIVATE -msse2 -msse3 -mssse3)
target_include_directories(nrsc5 PRIVATE ${CMAKE_SOURCE_DIR}/compat/nrsc5 ${N} PUBLIC ${EXT}/nrsc5/include)
target_link_libraries(nrsc5 PUBLIC faad_hdc fftw3f rtlsdr ws2_32 pthread)


# ---- the Android app's engine, compiled unchanged ----
add_library(engine STATIC ${HIDEF_CPP}/native-lib.cpp compat/winlog.cpp)
set_target_properties(engine PROPERTIES CXX_STANDARD 17)
target_compile_definitions(engine PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(engine PRIVATE -include ${CMAKE_SOURCE_DIR}/compat/engine_prefix.h)
target_include_directories(engine PUBLIC ${CMAKE_SOURCE_DIR}/compat/jni PRIVATE ${CMAKE_SOURCE_DIR}/compat ${HIDEF_CPP})
target_link_libraries(engine PUBLIC nrsc5 rtlsdr usb-1.0 fftw3f faad_hdc ws2_32)


# ---- the Windows app ----
add_library(core STATIC src/engine.cpp src/audio.cpp src/util.cpp src/signal.cpp src/stations.cpp
    src/store.cpp src/imaging.cpp src/mapfeed.cpp src/radio.cpp src/recorder.cpp)
set_target_properties(core PROPERTIES CXX_STANDARD 17)
target_compile_definitions(core PUBLIC _WIN32_WINNT=0x0A00 UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _USE_MATH_DEFINES)
target_link_libraries(core PUBLIC engine ole32 oleaut32 avrt windowscodecs shlwapi bcrypt uuid mfplat mfreadwrite mfuuid)

add_executable(HiDefRadio WIN32 src/main.cpp src/sigtext.cpp res/app.rc)
set_target_properties(HiDefRadio PROPERTIES CXX_STANDARD 17)
target_link_libraries(HiDefRadio core d2d1 dwrite comctl32)
target_link_options(HiDefRadio PRIVATE -static -municode -mwindows)

# The read-only data next to the exe: the same files the Android app has in its assets.
set(DATA_OUT ${CMAKE_BINARY_DIR}/data)
add_custom_command(TARGET HiDefRadio POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${DATA_OUT}/stations ${DATA_OUT}/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HIDEF_CPP}/../assets/stations/fm.txt ${HIDEF_CPP}/../assets/stations/am.txt ${DATA_OUT}/stations
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HIDEF_CPP}/../assets/usa_basemap.jpg ${DATA_OUT}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${HIDEF_CPP}/../res/font/dseg7_classic_bold.ttf ${DATA_OUT}/fonts/DSEG7Classic-Bold.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${HIDEF_CPP}/../assets/licenses ${DATA_OUT}/licenses)
