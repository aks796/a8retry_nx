# Notes: a8retry_nx

Technical notes for the Asphalt 8: Airborne Retry port. They cover what the
32-bit Switch libraries need, what this game needed, and what helps when
porting other 32-bit Android games.

The process runs in AArch32 mode. The game's engine is armeabi-v7a, and a
process cannot mix A32 and A64 code, so everything in it is 32-bit: devkitARM,
newlib, libnx32 and Mesa. Only the launcher NRO is 64-bit.

The host code started as the Disney Crossy Road port (hence the `dcr_*` file
names) and is shared with the Plants vs. Zombies TV Touch port. Most library
items below were found in one of the three and apply to all of them.

File paths are relative to this project.

---

## 1. Updates the 32-bit libraries need

This port builds against:

- **libnx32** ([github.com/aks796/libnx32](https://github.com/aks796/libnx32)):
  branch `master`, commit `41b61f92` (version 4.12.0). `build.sh` mounts
  its headers and archives over the toolchain image's.
- **mesa32**
  ([github.com/aks796/mesa32](https://github.com/aks796/mesa32)): Mesa
  20.1.0-rc3 and libdrm_nouveau 1.0.1, copied into `portlibs32/` from its
  `prefix/` or its release tarball.

Most of what this port found has been fixed in those libraries (1.1). The
port still carries its own versions of several of them (1.2), because those
are the ones tested on hardware. The rest is still open (1.3).

### 1.1 Fixed in the libraries

**libnx32 `cb01ef9f`: IPC data that depended on the size of an enum.**

- devkitARM builds with short enums, so an IPC call that sent an enum, or an
  array of enums, as raw data sent one byte per value.
- Here, `hidSetSupportedNpadIdType` (called by `padConfigureInput`) sent the
  `HidNpadIdType` list as bytes. hid read them as 32-bit IDs that match no
  slot, so only the attached Joy-Cons worked, and wireless controllers never
  connected.
- The fork declares those IPC fields as fixed-width integers.

**libnx32 `c6c53d20`: what the 32-bit ports worked around.**

| Problem | How it showed here | This port's own code |
| --- | --- | --- |
| `svcSetThreadCoreMask` took the mask as `u32`, but the kernel reads r2:r3. | r3 held the thread handle from `mutexUnlock`, so the kernel returned InvalidCoreId and every thread stayed on core 0. libnx's own `pthread_create` failed the same way. | `dcr_sched.c`, `dcr_thread_set_cores`, plus `__wrap_svcSetThreadCoreMask` (Makefile `--wrap`) |
| The `svcGetThreadCoreMask` stub pushed three registers and restored two. | An unbalanced stack. | `dcr_sched.c`, `dcr_thread_get_cores` |
| No stubs for WaitForAddress (0x34), SignalToAddress (0x35) or GetThreadContext3 (0x33). | Needed for pthreads and the watchdog. | `bionic_pthread.c`, `arb_wait_if_equal` and `arb_signal`; `watchdog.c` and `a8r_prof.c` issue 0x33 |
| `kernel/virtmem.c`: region ends in `uintptr_t` wrap to 0 at 0x1_0000_0000, and it searched the whole ASLR region. A 32-bit process may place shared and code mappings only in [0x200000, 0x40000000). | `MapSharedMemory` failed with InvalidCurrentMemory inside `hidInitialize`. | `nx32_virtmem.c` |
| `__libnx_initheap` asked for TotalMemory - Used, but a 32-bit heap region is 1 GiB. | `svcSetHeapSize` failed before `main`. | `nx_init.c` (also keeps `GFX_RESERVE_MB` back) |
| audout's buffer descriptor is 64-bit for every client, but `AudioOutBuffer` has 32-bit pointers. | Wrong-size IPC data. | `opensles.c`, `AoBuf` (0x28 bytes), sent through `serviceDispatch` |
| `__libnx_exception_entry` was a TODO stub. | Faults could not be handled in the process. | `exc32.S` and `exc_handler.c`, which write `crash.log` |
| `armICacheInvalidate` was `(void)0`: 32-bit EL0 has no cache maintenance instructions. | Stale instruction-cache lines after patching code. | `code_flush.c` (data-cache clean, then a code page's R/RX permission flip) |
| No way to get a real handle to the running process. `CUR_PROCESS_HANDLE` is refused by `svcMapProcessCodeMemory` and `svcMapProcessMemory`. | The loader could not map code. | `selfproc.c` and `crt0_reloc.c` (a session to itself) |
| The default window keeps its `ViDisplay` private, and vi allows one open display per process. | The vsync event could not be fetched. | `nx_init.c` defines `nwindowGetDefault`, `__nx_win_init` and `__nx_win_exit` |
| fsdev reported FS result 2-0007 (0xE02, a file open for writing) as EIO. | Confusing errors. It is now EBUSY. | `bionic_io.c` logs `fsdevGetLastResult()` for either |
| `timespec_get` was declared by newlib but not implemented. | Mesa's C11 threads need it. | `host_compat.c` |
| `switch32.ld` placed only `.rela.*`, but ARM uses SHT_REL. | `.rel.dyn` misplaced. | `dcr32.ld` |

**mesa32: commits on top of devkitPro's `switch-20.1.0-rc3` branch.** Earlier
builds of this port patched its own copy of `libEGL.a`. The fixes have moved
into mesa32, and `portlibs32/` is now mesa32's prefix as it is.

| Commit | File | Problem |
| --- | --- | --- |
| `2c27955c` nvc0: allow ETC2 and ASTC on the Switch | `nvc0_screen.c` | ETC1/ETC2 and ASTC were enabled only for chipset 0x12b, but the Switch's libdrm reports 0x120. Every ETC1 texture was decoded to RGBA in software, which slowed race loads and doubled texture memory. |
| `dddc69a4` st/mesa: don't crash on a texture attachment without storage | `st_cb_fbo.c` | Render-to-texture on a texture without storage dereferenced NULL in a release build. This is a generic Mesa bug. |
| `972de9c1` egl/switch: glthread support | `st_manager.c`, `st_context.c`, `egl_switch.c` | devkitPro's Switch branch compiled out glthread's `start_thread`, `thread_finish` and `_mesa_glthread_destroy` under `__SWITCH__`, and the Switch EGL platform had no way to start glthread. The commit brings those back and adds `switch_egl_start_glthread()` and a weak `switch_egl_glthread_hook()` that runs on the worker thread, and finishes the worker's queue before make-current and swap. `source/a8r_boot.c` and `source/a8r_perf.c` use both. |
| `24aa14fe` util: newlib's thrd_success is not 0 | `u_thread.h` | newlib's `thrd_success` is 4 (FreeBSD's value), and `u_thread_create` treated any non-zero result as failure. Mesa's queues gave up on threads that were running, so glthread never started. |
| `4e41d89f` egl/switch: set the window surface size | `egl_switch.c` | `eglQuerySurface` returned 0 for `EGL_WIDTH` and `EGL_HEIGHT`. `b_eglQuerySurface` (`source/gl_mesa.c`) still answers from `nwindowGetDimensions`. |
| `099a02a3` AArch32: don't depend on int-sized enums | `st_format.c`, `glformats.c`, `formats.c` | `mesa_format` is 16 bits wide under short enums, but it carries `MESA_ARRAY_FORMAT` values with bit 31 set. They were truncated and crashed format lookups. |

### 1.2 Workarounds this port keeps

These duplicate fixes that are now in the libraries (1.1). They stay because
they are what ran on hardware; each can be dropped after a hardware test with
the library's version.

- `a8r_input.c`, `set_supported_npad_ids()`: sends the supported controller
  list again as u32 IDs with `serviceDispatchIn(hid, 102)`.
- `dcr_sched.c`: raw `svc 0x0F` and `svc 0x0E` for thread core masks.
  `__wrap_svcSetThreadCoreMask` still routes libnx's own calls there (with the
  fork's u64 signature).
- `bionic_pthread.c`: its own WaitForAddress with both register layouts.
  - int64 value: r0 address, r1 type, r2:r3 value, r4:r5 timeout.
  - int32 value: r0 address, r1 type, r2 value, r3:r4 timeout.
  - On hardware (Atmosphère for firmware 21.x) and on Ryujinx 1.1.1098, timed
    waits were correct only with the int32 layout, the one the fork's stub
    uses.
  - The port starts with the int32 layout, and `dcr_pthread_selftest()`
    switches layouts if a 30 ms timed wait comes out wrong.
- `nx32_virtmem.c`, `nx_init.c` (heap, window) and `exc32.S`: they define the
  same symbols as the fork's code, so theirs win the link.
- `opensles.c`: its own audout append and get-released calls.

### 1.3 Still open

**libnx32.**

- **Text relocations.** devkitARM's prebuilt newlib, libsysbase and libstdc++
  are not built with `-fPIC`, so a PIE link has `R_ARM_RELATIVE` relocations
  in .text and .rodata. Making a code page writable turns it into CodeData
  on Mesosphère, which can never be executable again (svcBreak 0xDC03).
  - `source/crt0_reloc.c` replaces `__nx_dynamic`, together with
    `dcr32.specs` (`-z notext`) and `dcr32.ld` (page 0 reserved for crt0).
  - It maps each kernel memory block of .text and .rodata to a writable alias
    with `svcMapProcessMemory`, one block per call (the kernel refuses mixed
    ranges with 0xD401), and applies the relocations through the alias.
  - Building the target libraries with `-fPIC` would remove the need for
    this.
- **`__appInit` aborts on any service failure.** Under Ryujinx the time
  service's shared memory fails to map for 32-bit processes.
  `source/nx_init.c` records each result in `g_nxinit` instead, and
  `source/bionic_time.c` falls back to the system tick.
- **`exit()` shuts services down while other threads still run.** A game
  thread that polls touch after `hidExit` aborts the process (0x1159).
  `source/bionic_core.c`, `end_process()`, uses `svcExitProcess`, and
  `source/a8r_boot.c`, `exit_guard`, ends the process if the game has not
  closed within 5 s.
- **`CondVar` loses a signal when no thread is waiting.** The bionic condition
  variables are signal counters on the address arbiter
  (`source/bionic_pthread.c`), and each wait returns within 250 ms.
- **The console and EGL share the default window badly.** The console uses 2
  buffer slots and Mesa uses 3, and giving the window back to the console
  after Mesa fails on the third dequeue (0x2B59). `source/util.c`,
  `log_console_close()`, hands the window to EGL for good.

**devkitARM newlib.**

- **setjmp does not save d8-d15** (soft-float build). `source/bionic_setjmp.S`
  provides the bionic one, with a 256-byte `jmp_buf`.
- **libm is soft-float.** Every double operation is a library call.
  `source/bionic_math.c` implements the hot functions with VFP instructions.
- **`stat()` opens the file to read its size**, which fails on a file the
  process holds open for writing. `source/bionic_io.c` sizes such files
  through the open handle, and `dcr_io_selftest()` checks this at boot.
- **`access()` is unreliable over fsdev.** This port uses `stat()`.
- **The stdio buffer is 1 KB.** `b_fopen` gives read-only files a 32 KB
  buffer (`source/bionic_stdio.c`).
- **newlib and bionic differ in almost every structure.** The table in
  `source/bionic.h` covers `time_t`, `off_t`, `timespec`, `stat`, `dirent`,
  `pthread_mutex_t`, `jmp_buf`, `O_*` flags, clock IDs, errno values above
  34, `mbstate_t` and `LC_ALL`. This is inherent to running bionic code on
  newlib, not a bug.

**miniz (zlib in the toolchain image).** Its `inflate` is greedy: it consumes
all of its input, including the Adler-32 trailer, while output is still owed.
libpng 1.5.9 then asks for more data when `avail_in` is 0 and fails with "Not
enough image data". `source/bionic_zlib.c` hands one consumed byte back when
the output is full and the input is empty. A real zlib, or this fix in miniz,
is needed by any program that uses libpng on this toolchain.

**Mesa's Switch EGL platform.**

- It has window surfaces only. `b_eglCreatePbufferSurface` returns
  `EGL_NO_SURFACE`, and the engine's extra loading contexts are made
  surfaceless (`source/a8r_boot.c`, `egl_up`).
- It offers RGBA8888 window configs without MSAA and rejects Android-only
  attributes. `filter_attrs` (`source/gl_mesa.c`) drops them, and config
  requests are retried without MSAA.
- Display buffers are dequeued without a fence, so `nwindowDequeueBuffer`
  blocks until the GPU is done with a buffer. `source/a8r_perf.c` measures
  this with `__wrap_nwindowDequeueBuffer`.

### 1.4 Rules for code on these libraries

- **Inline SVCs must clobber r1-r3.** AArch32 SVCs return with r1-r3 zeroed.
  The watchdog's first report crashed because the compiler had kept a pointer
  in r3 across an inline SVC (`source/watchdog.c`, `get_ctx`).
- **Only priority 59 (cores 0-2) and priority 63 (core 3) are time-sliced**,
  every 10 ms. Android code that spin-waits hangs at any other priority.
  Threads created with `threadCreate(..., -2)`, and the main thread, get an
  affinity of their ideal core only. `source/dcr_sched.c` moves the game's
  threads to priority 59 on cores 0-2, and `sched_selftest` checks the
  time-slicing at start-up.
- **Code pages are never made writable.** `source/so_util.c`,
  `so_patch_code`, writes through a temporary alias, one kernel memory block
  per call.
- **Nothing is mapped at the Linux kuser page.** Code built with old libgcc
  calls 0xffff0fc0 (cmpxchg) and 0xffff0fa0 (barrier). `source/kuser.S`
  provides both, and `source/a8r_loader.c` repoints the engine's literals to
  them (44 cmpxchg and 9 barrier sites in this engine).

### 1.5 Toolchain and launch setup

- **Toolchain image.** `ghcr.io/vita2hos/devcontainer/vita2hos:latest`
  contains devkitARM with GCC multilibs, switch-tools, libnx32 and miniz.
  `build.sh` runs `make` inside it, with the libnx32 fork mounted over the
  image's `include/switch`, `switch.h`, `libnx.a` and `libnxd.a`. It does not
  mount the whole folder, because the image keeps miniz there.
- **Launch method.** A 32-bit program cannot be an NRO, because hbloader is
  64-bit.
  - This port ships a 64-bit launcher NRO (`launcher/`) that carries the
    32-bit ExeFS NSP in its romfs.
  - Started from a sphaira forwarder, it writes
    `atmosphere/contents/<title id>/exefs.nsp` with `main.npdm` retargeted to
    that title (`source/dcr_exefs.h`) and restarts the title.
  - The hbl override with `override_any_app_address_space=32_bit` also works.
- **Ryujinx 1.1.1098 gaps** (emulator only, handled in the port):
  - no A32 VCVT between float and fixed point (`source/emu_fixups.c`);
  - no T32 DMB, DSB or ISB (`source/so_util.c`,
    `emu_strip_thumb_barriers`);
  - no NEON `vsri` (`source/a8r_java.c` uses a scalar MD5);
  - boost's Thumb `thread_proxy` fails to decode (`source/a8r_loader.c`);
  - `svcMapProcessCodeMemory` destinations mapped with no permission;
  - the pseudo-handle refused;
  - 16 KiB host pages on Apple silicon (`source/crt0_reloc.c` keeps aliases
    64 KiB-aligned).

---

## 2. What this game needed

**The game.** Asphalt 8: Airborne 1.6.0 (versionCode 16000,
`com.gameloft.android.HEP.GloftA8HP`), armeabi-v7a, Gameloft's jet/glf
engine, as modified by the Asphalt 8: Airborne Retry mod (A8R, December build,
by Techboy1997). The data is the mod's `gameloft/games/GloftA8HP` folder: a
1.5 GB OBB plus loose files from the December update.

**Engine assembly** (`source/a8r_setup.c`).

- The mod does not ship `libasphalt8.so` as a file. It hides it in five APK
  assets named like music (`m_*.mp3`), each a zip stored byte-reversed with
  every byte pair swapped.
- The zips hold 71 pieces, plus 1-20 byte alternatives for the pieces that
  the mod's options patch.
- `a8r_setup()` puts the library together as the mod's PLAY button does, from
  `config.ini` [mod], [graphics] and [quick_race]. It redoes this only when
  the APK or those options change (the `.setup` stamp).
- It also prepares the data folder: texts with the nickname written in,
  `gameprofiles.txt` (every graphics switch is an edit of it), overrides and
  the soundpack.

**The start screen** (`source/a8r_menu.c`, `a8r_ui.c`, `a8r_arsc.c`,
`a8r_menu_audio.c`).

- On Android, the mod opens on its own Java screen before the game. The port
  redraws it natively with GLES 2 and stb_truetype: the mod's layout, strings
  (from `resources.arsc`, seven languages), images, music (minimp3) and
  sounds, all read from the user's APK.
- A fourth options page, SWITCH, holds the port's own settings.

**Setup progress.** First-run setup, updates and new files show a green
progress bar with the game's name and the current step, the same screen as
the other 32-bit ports. A normal start shows nothing.

- `source/util.c`, `log_console_progress`, draws it on the boot console.
- `source/a8r_menu.c`, `a8r_menu_progress`, draws the same screen with the
  start screen's renderer when setup runs after PLAY. By then EGL has the
  window, and the console cannot take it back (1.3). The start screen keeps
  the window until `a8r_menu_done`.
- `source/a8r_setup.c`, `setup_progress`, stages the steps in permille.
- `launcher/source/zips.c`, `launcher_bar`, shows it while the launcher
  extracts the mod's zips.

**Loader** (`source/so_util.c`, `source/a8r_loader.c`).

- ELF32 relocations are SHT_REL, so the addend is the word already at the
  target.
- `so_relocate` applies local fixups first, then `so_resolve` binds imports.
- Hooks are 8-byte `LDR PC,[PC,#-4]` stubs, which also reach Thumb code.
- `libasphalt8.so` is 28 MB of code. It is loaded into a 40 MB region
  (`SO_REGION_BYTES`), and its 244 constructors run at System.load time
  (`a8r_run_constructors`).
- `patch_steering` changes the engine so that the left stick steers in
  proportion. The engine only has proportional steering on its racing-wheel
  path, and reads SHIELD sticks as digital.

**Imports.** `source/imports.c` is generated by `tools/gen_imports.py` from
`tools/imports_needed.txt`. Each import maps, in order:

1. to a `b_<symbol>` shim;
2. to a data object;
3. to a newlib function whose ABI agrees (PASSTHROUGH);
4. to NULL, for weak symbols.

`gl*` names resolve through `source/gl_mesa.c`.

**No ELF TLS in the guest.** The loader handles only ABS32, GLOB_DAT,
JUMP_SLOT and RELATIVE relocations. Per-thread data goes through pthread keys
(`source/bionic_pthread.c`). The host is built with `-mtp=soft
-ftls-model=local-exec`.

**Bionic shims** (`source/bionic_*.c`).

- They cover libc, stdio, files, time, pthreads, memory (mmap from the heap),
  signals (recorded, never delivered), dl and networking.
- Networking is offline: sockets can be created but never connect
  (`bionic_net.c`).
- `bionic_a8r.c` adds what this engine needs beyond the other ports:
  `__cxa_guard_*` (about 900 call sites), nothrow new and delete, `__srget`,
  `freopen` and `pread`.

**JNI** (`source/jni_core.c`, `source/a8r_java.c`).

- All 233 JNIEnv slots are filled, and calls are decoded from the method
  signature.
- `a8r_java.c` answers about 200 of the game's static Java methods: device
  info, paths, SharedPreferences (stored as Android XML, `dcr_prefs.c`),
  `GetProfilesStr`, `getGLUID`, and `retrieveBarrels`. The last one needs the
  APK's signing certificate hash.
- `classes.txt`, the class list from the APK's dex, backs `FindClass`.

**Audio** (`source/opensles.c`). The engine's audio is told API level 9, so it
always uses OpenSL ES. `opensles.c` implements the engine, output mix and
buffer-queue player objects. An audio thread resamples 44.1 kHz to 48 kHz and
feeds three 1024-frame audout buffers.

**Input** (`source/a8r_input.c`).

- The port reports an "NVIDIA Controller" (SHIELD, kind 5), whose layout the
  Pro Controller shares. Keys go through `GL2JNILib.keyEvent`; sticks and
  triggers go through `nativeSetPowerA*`.
- The engine counts a stick only at exactly ±1.0, so the throw is rescaled:
  nothing below 12%, and 1.0 from 85%.
- In races (`KeyboardControl::IsRacing`), a Switch-style layout applies by
  default: A accelerate, B brake, ZR/R drift, ZL/L/Y nitro.
- `source/a8r_card.c` replaces the game's SHIELD controller card with a Pro
  Controller card. The texture is matched by CRC as the engine uploads it.

**GL** (`source/a8r_boot.c`, `source/gl_mesa.c`).

- `egl_up` requests GLES 3, falling back to 2. It makes up to
  `[display] loading_contexts` surfaceless shared contexts for the engine's
  loading threads.
- Optionally, it starts Mesa's glthread for the game's context
  (`[performance] gl_thread`).
- The GL thread is created at priority 58 on core 1.

**Files and paths** (`source/dcr_path.c`, `source/bionic_io.c`,
`source/dcr_dircache.c`, `source/dcr_apkcache.c`).

- Android paths map into the game folder: `/storage/emulated/0/gameloft`,
  `/sdcard`, `/data/data/<package>` and `base.apk` (to `A8R.apk`).
- `bionic_io.c` fakes `/proc`, `/dev/urandom`, `/dev/null`, pipes and auxv
  with fds from 0x4000 up.
- The engine reads the OBB's zip directory (8,218 entries) in 4-byte reads,
  about 300,000 of them. Read-only files get read-ahead.
- `dcr_dircache.c` answers missing-file lookups from cached, case-insensitive
  directory listings.
- `dcr_apkcache.c` keeps 128 KB blocks of the APK in RAM.

**Performance** (`source/a8r_perf.c`, `source/dcr_boost.c`). A locked 60 fps
in races took four changes:

1. The CPU at 1785 MHz through clkrst (pcv before 8.0.0). The system resets
   the clock, so it is re-applied every 250 ms. It goes back to 1020 MHz when
   the HOME menu is up and on exit. Configurable: `[performance] cpu_clock`.
2. The handheld GPU at 460.8 MHz (apm configuration 0x92220007).
3. The GL thread one priority above the other guest threads (58), so that
   the kernel's time slicing does not preempt it.
4. Mesa's glthread on a second core (mesa32's `972de9c1`, 1.1).

With these, the GL thread takes 7-10 ms a frame, and races miss 0-12 of 600
refreshes per 10 s on hardware. What remains is about 16 `glMapBuffer` calls
a frame, each of which makes glthread finish its queue on the GL thread.
`a8r_perf.c` logs per-frame CPU, buffer-wait and GPU times
(EXT_disjoint_timer_query) and glthread syncs per GL call.

**Engine quirks.**

- The engine calls `_exit` on its app thread.
- It reopens the OBB about five times a second while racing.
- Its static destructors are never run (`b___cxa_atexit` is a no-op).
- A 5 s splash timer gates the controller announcement.
- On focus loss the port runs onPause, pauses audio and suspends the
  monotonic clock (`source/a8r_boot.c`, `apply_focus`).
- `source/watchdog.c` flushes the log and snapshots every thread after 10 s
  without a frame.

**The game folder.**

- Launcher and wrapper share two helpers in `source/a8r_folder.h`:
  - `a8r_migrate` moves the first builds' folder, `/switch/a8retry`, into
    `/switch/a8retry_nx`.
  - `a8r_adopt_apk` accepts the mod's APK under any file name, recognized by
    its contents.
- The mod's zips are recognized by their contents too (`source/a8r_zips.h`).

---

## 3. Porting other 32-bit Android games

1. **Build softfp.** armeabi-v7a, libnx32 and devkitARM's newlib all pass
   floats in core registers. With softfp, shims need no `pcs("aapcs")`
   annotations and can still use VFP and NEON. Use `-mtp=soft
   -ftls-model=local-exec -fPIE` as well (see the Makefile's `ARCH`).
2. **Plan the launch.** A 32-bit program cannot be an NRO. Ship an ExeFS NSP
   (`main` + `main.npdm`), launched through a 64-bit launcher that installs
   an Atmosphère ExeFS override for a forwarder title, or through the hbl
   override with `override_any_app_address_space=32_bit`. The NPDM
   (`a8retry_nx.json`) needs:
   - `is_64_bit` false and `address_space_type` 2;
   - thread priorities up to 59 and cores 0-2;
   - SVCs 0x73-0x78 (process and code memory), 0x34 and 0x35 (arbiter) and
     0x5F (data-cache flush).
3. **The address space is small.** The heap region is 1 GiB. Code and shared
   mappings go only in [0x200000, 0x40000000). GPU buffers come out of the
   heap.
4. **Audit libnx32 for 32-bit hazards:** u32 prototypes where the kernel takes
   64-bit values, IPC structs with pointer, `size_t` or enum fields, and
   missing SVC stubs. Put raw SVCs behind a start-up self-test, as
   `dcr_sched.c`, `bionic_pthread.c`, `bionic_io.c` and `opensles.c` do.
5. **Never make code writable.** Patch through `svcMapProcessMemory`
   aliases, one kernel memory block per call. This needs a real process handle
   (`selfproc.c`). I-cache invalidation needs the permission-flip page
   (`code_flush.c`).
6. **Expect text relocations in the host.** They are unavoidable with
   devkitARM's target libraries. Reuse `dcr32.specs`, `dcr32.ld` and
   `crt0_reloc.c`.
7. **Put every guest thread at priority 59, cores 0-2.** Horizon only
   time-slices there, and Android-style spin-waits hang elsewhere. Keep the
   render thread one step higher.
8. **Treat bionic and newlib as different ABIs.** Convert every struct, flag,
   errno value and clock ID (`bionic.h`), and pass straight through only what
   is known to match. Redirect kuser helper literals. Expect no ELF TLS.
9. **Watch short enums.** Any library built for arm-none-eabi, and any struct
   field or bitfield typed as an enum, may be truncated.
10. **Filesystem.** Horizon allows one writer per file. Every small read is
    an IPC round trip, so use read-ahead, directory caches and large stdio
    buffers. libnx's EIO hides the real result: log `fsdevGetLastResult()`.
11. **End the process with `svcExitProcess`, not `exit()`.** Once EGL owns
    the window, never give it back to the console.
12. **Test on Ryujinx knowing its gaps** (section 1). `crt0_reloc.c`'s
    pseudo-handle probe is a reliable emulator check. Judge performance only
    on hardware.
