/* UQM build configuration for the Nintendo 3DS (stands in for the
 * config_unix.h that build.sh generates on desktop systems).
 * Paths have no "sdmc:" prefix: libctru makes the SD card the default. */
#ifndef CONFIG_UNIX_H_
#define CONFIG_UNIX_H_

#define CONTENTDIR "/3ds/uqm/content"
#define USERDIR "/3ds/uqm/"
#define CONFIGDIR USERDIR
#define MELEEDIR "${UQM_CONFIG_DIR}/teams/"
#define SAVEDIR "${UQM_CONFIG_DIR}/save/"

#undef WORDS_BIGENDIAN
#define HAVE_READDIR_R         /* libsysbase provides it */
#define HAVE_SETENV
#undef HAVE_STRUPR
#define HAVE_STRCASECMP_UQM
#undef HAVE_STRICMP
#define HAVE_GETOPT_LONG
#define HAVE_ISWGRAPH
#define HAVE_WCHAR_T
#define HAVE_WINT_T
#define HAVE__BOOL

#endif  /* CONFIG_UNIX_H_ */
