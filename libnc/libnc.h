#if !defined(__LIBNC_H__)
#define __LIBNC_H__

#include "nc_defines.h"
#include "nc_types.h"
#include "nc_system.h"
#include "nc_memory.h"

#if defined(NC_CC_GNU)
    #include "linux/linux_system.cpp"
#endif

#include "nc_tls.h"
#include "nc_math.h"
#include "nc_arena.h"
#include "nc_string.h"
#include "nc_time.h"
#include "nc_print.h"
#include "nc_ring.h"
#include "nc_log.h"
#include "nc_profile.h"
#include "nc_atomics.h"
#include "nc_cli.h"
#include "nc_file.h"
#include "nc_net.h"
#include "nc_http.h"
#include "nc_proc.h"
#include "nc_simd.h"
#include "nc_sync.h"
#include "nc_thread.h"
#include "nc_object_cache.h"
#include "nc_content.h"
#include "nc_file_stream.h"
// #include "nc_edit.h"
#include "nc_base64.h"
#include "nc_hash.h"
#include "nc_sock.h"
#include "nc_keyboard.h"
#include "nc_font_provider.h"
#include "nc_font_cache.h"
#include "nc_render.h"
#include "nc_draw.h"
#include "nc_ui.h"
#include "nc_ui_components.h"
#include "nc_window.h"
#include "nc_config.h"
#include "nc_metadesk.h"
#include "nc_sort.h"
#include "nc_application.h"
#include "nc_application_eval.h"
#include "nc_application_views.h"
#include "nc_eval.h"

#include "nc_math.cpp"
#include "nc_memory.cpp"
#include "nc_arena.cpp"
#include "nc_string.cpp"
#include "nc_system.cpp"
#include "nc_time.cpp"
#include "nc_ring.cpp"
#include "nc_log.cpp"
#include "nc_profile.cpp"
#include "nc_cli.cpp"
#include "nc_file.cpp"
#include "nc_http.cpp"
#include "nc_proc.cpp"
#include "nc_thread.cpp"
#include "nc_object_cache.cpp"
#include "nc_content.cpp"
#include "nc_file_stream.cpp"
// #include "nc_edit.cpp"
#include "nc_base64.cpp"
#include "nc_hash.cpp"
#include "nc_keyboard.cpp"
#include "nc_font_cache.cpp"
#include "nc_render.cpp"
#include "nc_draw.cpp"
#include "nc_ui.cpp"
#include "nc_ui_components.cpp"
#include "nc_window.cpp"
#include "nc_metadesk.cpp"
#include "nc_config.cpp"
#include "nc_application.cpp"
#include "nc_application_eval.cpp"
#include "nc_application_views.cpp"
#include "nc_eval.cpp"

#if defined(NC_OS_WIN)
    #include "win32/win32_platform.h"
    #include "win32/win32_system.cpp"
    #include "win32/win32_memory.cpp"
    #include "win32/win32_file.cpp"
    #include "win32/win32_keyboard.h"
    #include "win32/win32_net.cpp"
    #include "win32/win32_http.cpp"
    #include "win32/win32_proc.cpp"
    #include "win32/win32_sync.cpp"
    #include "win32/win32_thread.cpp"
    #include "win32/win32_time.cpp"
    #include "win32/win32_window.cpp"
    #include "win32/win32_print.cpp"
    #include "win32/win32_sock.cpp"
    #include "win32/win32_entry.cpp"
#elif defined(NC_OS_LINUX)
    #include "linux/linux_platform.h"
    #include "linux/linux_memory.cpp"
    #include "linux/linux_time.cpp"
    #include "linux/linux_file.cpp"
    #include "linux/linux_net.cpp"
    #include "linux/linux_proc.cpp"
    #include "linux/linux_sync.cpp"
    #include "linux/linux_thread.cpp"
    #include "linux/linux_print.cpp"
    #include "linux/linux_window.cpp"
    #include "linux/linux_sock.cpp"
    #include "linux/linux_entry.cpp"
#elif defined(NC_OS_MAC)
    #include "macos/macos_system.cpp"
    #include "macos/macos_memory.cpp"
    #include "macos/macos_file.cpp"
    #include "macos/macos_keyboard.h"
    #include "macos/macos_net.cpp"
    #include "macos/macos_proc.cpp"
    #include "macos/macos_sync.cpp"
    #include "macos/macos_thread.cpp"
    #include "macos/macos_time.cpp"
    #include "macos/macos_window.cpp"
    #include "macos/macos_print.cpp"
#endif

#if defined(NC_OS_WIN) && defined(NC_USE_DX11)
    #include "dx11/dx11_platform.h"
    #include "dx11/dx11_render.h"
    #include "dx11/dx11_render.cpp"
#elif defined(NC_USE_OPENGL)
    #include "opengl/opengl_platform.h"
    #include "opengl/opengl_render.h"

    #include "opengl/opengl_render.cpp"

    #if defined(NC_USE_OPENGL_GLX)
        #include "opengl/opengl_glx_platform.h"
        #include "opengl/opengl_glx.h"

        #include "opengl/opengl_glx.cpp"
    #elif defined(NC_USE_OPENGL_EGL)
        #include "opengl/opengl_egl_platform.h"
        #include "opengl/opengl_egl.h"

        #include "opengl/opengl_egl.cpp"
    #else
        #error Need to specify backend of OpenGL to use [GLX | EGL]
    #endif
#elif defined(NC_USE_VULKAN)
    #error NC_USE_VULKAN defined but no vulkan backend defined
#elif defined(NC_OS_MAC) && defined(NC_USE_METAL)
    #error NC_USE_METAL defined but no metal backend defined
#else
    #error NC_GUI defined but no gfx backend defined should be NC_USE_[DX11 | OPENGL | VULKAN | METAL]
#endif

#if defined(NC_OS_WIN) && defined(NC_USE_DWRITE)
    #include "dwrite/dwrite_platform.h"
    #include "dwrite/dwrite_font_provider.h"
    #include "dwrite/dwrite_font_provider.cpp"
#elif defined(NC_USE_FREETYPE)
    #include "freetype/freetype_platform.h"
    #include "freetype/freetype_font_provider.h"
    #include "freetype/freetype_font_provider.cpp"
#else
    #error NC_GUI defined but no font backend defined should be NC_USE_[DWRITE | FREETYPE]
#endif

// NOTE(nc): X11 defines plain words as macros, and they reach every file
//         : after this one. the platform code is all above this line, so
//         : they end here.
#if defined(NC_OS_LINUX)
    #undef Status
    #undef Bool
    #undef True
    #undef False
    #undef None
    #undef Success
    #undef Always
    #undef Above
    #undef Below
    #undef Opposite
    #undef Complex
    #undef Convex
    #undef Unsorted
#endif

#include "rad_lzb_simple/rad_lzb_simple.h"
#include "rad_lzb_simple/rad_lzb_simple.c"
#include "martins_hash/md5.h"

#if defined(NC_DEBUG)
    void
    AppRegisterLogEndpoints(void)
    {
        PROFILE(LogConsoleSetupTimer) {
            // console
            LogEndpointParams LogConsoleParams = {
                "CONSOLE"_s8,
                LOG_ENDPOINT_FUNC_NAME(Console),
                LOG_ENDPOINT_FLUSH_FUNC_NAME(Console),
                NULL,
                0,
                LOG_LEVEL_KIND_NORMAL
            };

            LogRegisterEndpoint(LogConsoleParams);
        }
    }

    void
    AppUnregisterLogEndpoints(void)
    {}
#endif

ASYNC_THREAD_FUNC_SIG(EntryPoint);
void FLIInit(CommandLine* CLI);

void
MainThreadBaseEntryPoint(int ArgC, char** ArgV)
{
    ThreadSetName("[MAIN]"_s8);

    TempArena Scratch = GetScratch(NULL, 0);

    ASYNC_TICK_BEGIN_COND_VAR = CondVarAlloc();
    ASYNC_TICK_BEGIN_MTX = MutexAlloc();
    ASYNC_TICK_END_MTX = MutexAlloc();

    Str8List CLIStrings = StrListFromCLIArgs(Scratch.MemPool, ArgC, ArgV);
    CommandLine CLI = CommandLineFromStrList(Scratch.MemPool, CLIStrings);

    LogInit();

#if defined(NC_DEBUG)
    AppRegisterLogEndpoints();
#endif

    OCInit();
    CInit();
    FSInit();
    // EditInit();

#if !defined(NC_CLI)
    GraphicsInit();
    RendInit(&CLI);
    FontProviderInit();
    FontInit();
    UIInit();
#endif

    FLIInit(&CLI);

    Handle* AsyncThreads = NULL;
    u64 LaneBroadcastValue = 0;
    u64 MainThreadCount = 1;
    u64 AsyncThreadsCount = GetSystemProperties()->LogicalProcessorCount;
    u64 MainThreadCountClamped = MIN(AsyncThreadsCount, MainThreadCount);

    AsyncThreadsCount -= MainThreadCountClamped;

    Str8 AsyncThreadsCountString = CommandLineString(&CLI, "threads"_s8);

    if (AsyncThreadsCountString.Size)
        AsyncThreadsCount = U64FromStr(AsyncThreadsCountString);

    AsyncThreadsCount = MAX(1, AsyncThreadsCount);

#if defined(NC_CLI)
    AsyncThreadsCount = 1;
#endif

    Handle Barrier = BarrierAlloc(AsyncThreadsCount);
    LaneContext* LaneCtxs = ArenaPushArrayZero(
        Scratch.MemPool, 
        LaneContext, 
        AsyncThreadsCount
    );

    ASYNC_THREADS_COUNT = AsyncThreadsCount;
    AsyncThreads = ArenaPushArrayZero(Scratch.MemPool, Handle, ASYNC_THREADS_COUNT);

    for (u64 Index = 0; Index < ASYNC_THREADS_COUNT; ++Index) {
        LaneCtxs[Index].Index = Index;
        LaneCtxs[Index].Count = ASYNC_THREADS_COUNT;
        LaneCtxs[Index].Barrier = Barrier;
        LaneCtxs[Index].BroadcastMemory = &LaneBroadcastValue;
        AsyncThreads[Index] = ThreadLaunch(
            ASYNC_THREAD_FUNC_NAME(EntryPoint), 
            &LaneCtxs[Index]
        );
    }

    EntryPoint(&CLI);
    AtomicIncFetchU32(&GLOBAL_ASYNC_EXIT, MEM_ORDER_SEQ_CST);
    CondVarBroadcast(ASYNC_TICK_BEGIN_COND_VAR);

    for (u32 Index = 0; Index < ASYNC_THREADS_COUNT; ++Index)
        ThreadJoin(AsyncThreads[Index], U64_MAX);

#if defined(NC_DEBUG)
    AppUnregisterLogEndpoints();
#endif

    ReleaseScratch(Scratch);
}

INTERNAL b32
Update(void)
{
    b32 Result = FALSE;

    AtomicIncFetchU64(&GLOBAL_UPDATE_TICK_INDEX, MEM_ORDER_SEQ_CST);
    FontFrame();
    Result = Frame();

    return Result;
}
#else
    #error libnc.h should only be included once in any file of your choosing
#endif // __LIBNC_H__
