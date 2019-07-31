#define TARGET_NAME "ep128"
#define TARGET_DESC "Enterprise 128"
#define CONFIG_Z180
#if defined(XEMU_ARCH_NATIVE) && !defined(__arm__)
#define XEP128_GTK
#endif
#ifdef CONFIG_Z180
#define Z80EX_Z180_SUPPORT
#endif
#define Z80EX_ED_TRAPPING_SUPPORT
#define Z80EX_CALLBACK_PROTOTYPE extern
#define CONFIG_SDEXT_SUPPORT
#ifdef __EMSCRIPTEN__
#define NO_CONSOLE
#endif

#define CONFIG_EMSCRIPTEN_OK
