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

// @defines____________________________________________________________________
#define CLI_FLIGHT_SLOTS_COUNT 4096

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
    WallClockTime Time;
    WallClockTime SourceTime;
};

struct CLIShared {
    Arena*      MemPool;
    CLIFlight** FlightSlots;
    CLIFlight*  HeadFlight;
    CLIFlight*  TailFlight;
    u64         FlightsCount;
    CLIFlight** Flights;
    CLIScript*  HeadScript;
    CLIScript*  TailScript;
    u64         ScriptsCount;
    Str8        OutputPath;
    Str8        FLIOutputPath;
    Str8Array   SearchPaths;
    u64         FlightTakeCounter;
    u64         ProgramsCount;
    u64         ExportedCount;
    u64         FreshCount;
    u64         FailCount;
    u64         ConvertedCount;
    b32         IsRecursive;
    b32         IsForced;
    b32         IsDryRun;
    b32         DoCompress;
};

// @runtime____________________________________________________________________
extern CLIShared* CLI_SHARED;

// @functions__________________________________________________________________
void CLIEntryPoint(CommandLine* CLI);
void CLIThreadEntryPoint(void* __Params);

#endif // __CLI_MAIN_H__
