include(CheckIncludeFile)
include(CheckFunctionExists)

check_include_file("sys/vfs.h" HAVE_SYS_VFS_H)
if(HAVE_SYS_VFS_H)
    add_compile_definitions(HAVE_SYS_VFS_H)
endif()

check_include_file("sys/stat.h" HAVE_SYS_STAT_H)
if(HAVE_SYS_STAT_H)
    add_compile_definitions(HAVE_SYS_STAT_H)
endif()

check_function_exists(posix_memalign HAVE_POSIX_MEMALIGN)
if(NOT HAVE_POSIX_MEMALIGN)
    add_compile_definitions(NO_POSIX_MEMALIGN)
endif()

check_function_exists(mlock HAVE_MLOCK)
if(HAVE_MLOCK)
    add_compile_definitions(USE_MLOCK)
endif()
