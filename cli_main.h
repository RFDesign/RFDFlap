#if !defined(__CLI_MAIN_H__)
#define __CLI_MAIN_H__

#include "nc_types.h"
#include "nc_string.h"
#include "nc_time.h"
#include "nc_thread.h"
#include "nc_cli.h"
#include "nc_file.h"

#include "fli.h"

struct Arena;
struct FLVUnit;
struct FLAPRasterFont;

// @defines____________________________________________________________________
#define CLI_FLIGHT_SLOTS_COUNT  4096
#define CLI_GRAPH_WIDTH         1600
#define CLI_GRAPH_HEIGHT         900
#define CLI_GRAPH_SIZE_MIN       256
#define CLI_GRAPH_SIZE_MAX     16384
#define CLI_GRAPH_FONT_SIZE       12.0f
#define CLI_TAB_WIDTH           1920
#define CLI_TAB_HEIGHT          1080

#define CLI_GRAPH_ROW_HEIGHT (2.0f * CLI_GRAPH_FONT_SIZE)
#define CLI_GRAPH_GRID_ALPHA 0.15f
#define CLI_TAB_ROW_ALPHA    0.15f

// @types______________________________________________________________________
struct CLIThreadParams {
    CommandLine* CLI;
    LaneContext  LaneCtx;
};

struct CLIFlight {
    CLIFlight*    Next;
    CLIFlight*    HashNext;
    Str8          BINPath;
    Str8          FLIPath;
    u64           BINSize;
    u64           FLISize;
    WallClockTime BINTime;
    WallClockTime FLITime;
    Handle        File;
    Handle        FileMap;
    void*         FileBase;
    FLIParsed     FLI;
    FLIKey        Key;
    b32           HasFLI;
    b32           IsFLIStale;
    b32           HasFailed;
    b32           IsOpen;
};

struct CLIScript {
    CLIScript*    Next;
    FLVUnit*      Unit;
    Str8          Path;
    Str8*         ProgramNames;
    Str8**        DirectiveNames;
    WallClockTime Time;
    WallClockTime SourceTime;
};

struct CLIGraphTicks {
    u64 TimeStepUSecs;
    u64 TimeFirstUSecs;
    f32 ValueSteps[2];
    f32 ValueFirsts[2];
    u32 TimeDigits;
    u32 __Padding;
};

struct CLIShared {
    Arena*          MemPool;
    CLIFlight**     FlightSlots;
    CLIFlight*      HeadFlight;
    CLIFlight*      TailFlight;
    u64             FlightsCount;
    CLIFlight**     Flights;
    CLIScript*      HeadScript;
    CLIScript*      TailScript;
    u64             ScriptsCount;
    Str8            OutputPath;
    Str8            FLIOutputPath;
    Str8Array       SearchPaths;
    Str8            GraphFontPath;
    FLAPRasterFont* GraphFont;
    u64             FlightTakeCounter;
    u64             ProgramsCount;
    u64             ExportedCount;
    u64             FreshCount;
    u64             FailCount;
    u64             ConvertedCount;
    v2i32           GraphSize;
    v2i32           TabSize;
    b32             IsRecursive;
    b32             IsForced;
    b32             IsDryRun;
    b32             DoCompress;
    b32             IsGraphCSV;
    b32             HasGraphGrid;
    b32             HasGraphModes;
    b32             HasGraphFlags;
};

// @runtime____________________________________________________________________
extern CLIShared* CLI_SHARED;

// @functions__________________________________________________________________
void CLIEntryPoint(CommandLine* CLI);
void CLIThreadEntryPoint(void* __Params);

#endif // __CLI_MAIN_H__
