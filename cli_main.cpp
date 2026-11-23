#include "nc_arena.h"
#include "nc_atomics.h"
#include "nc_memory.h"
#include "nc_print.h"
#include "nc_sync.h"
#include "nc_simd.h"
#include "nc_sort.h"
#include "fonts/nc_fonts.h"
#include "nc_font_provider.h"

#include "fli.h"
#include "fli_bin.h"
#include "fli_main.h"

#include "flv_bind.h"
#include "flv_compile.h"
#include "flv_run.h"
#include "flv_stdlib.h"
#include "flv_token.h"

#include "cli_main.h"

#include "flap_graph.h"
#include "flap_widgets.h"
#include "flap_export.h"
#include "flap_raster.h"

CLIShared* CLI_SHARED = NULL;

INTERNAL CLIFlight*
CLIFlightFromFLIPath(Str8 FLIPath, b32 AllocIfMissing)
{
    TempArena Scratch = GetScratch(NULL, 0);
    u64 PathHash = Hash(ToLower(Scratch.MemPool, FLIPath));
    u64 SlotIndex = PathHash % CLI_FLIGHT_SLOTS_COUNT;
    CLIFlight* Result = NULL;

    for (
        CLIFlight* Flight = CLI_SHARED->FlightSlots[SlotIndex];
        Flight;
        Flight = Flight->HashNext
    ) {
        if (StrMatch(Flight->FLIPath, FLIPath, PathMatchKindFromSystem())) {
            Result = Flight;
            break;
        }
    }

    if (!Result && AllocIfMissing) {
        Result = ArenaPushArrayZero(
            CLI_SHARED->MemPool,
            CLIFlight,
            1
        );
        Result->FLIPath = ArenaPushStrCpy(
            CLI_SHARED->MemPool,
            FLIPath
        );
        SLL_STACK_PUSH_EX(
            CLI_SHARED->FlightSlots[SlotIndex],
            Result,
            HashNext
        );
        SLL_QUEUE_PUSH(
            CLI_SHARED->HeadFlight, 
            CLI_SHARED->TailFlight, 
            Result
        );
        ++CLI_SHARED->FlightsCount;
    }

    ReleaseScratch(Scratch);

    return Result;
}

INTERNAL b32
CLIPushFile(Str8 RawPath, FileProperties Props, b32 AllocIfMissing)
{
    TempArena Scratch = GetScratch(NULL, 0);
    Str8 Path = PathNormalisedFromStr(Scratch.MemPool, RawPath);
    Str8 Ext = StrSkipLastDot(Path);
    b32 Result = TRUE;

    if (StrMatch(Ext, "bin"_s8, STR_MATCH_ALL_CASES)) {
        Str8 FLIPath = PathReplaceFileExtension(
            Scratch.MemPool,
            Path,
            "fli"_s8
        );

        if (CLI_SHARED->FLIOutputPath.Size) {
            FLIPath = PathNormalisedFromStr(
                Scratch.MemPool,
                ArenaPushStrFmt(
                    Scratch.MemPool,
                    "%S/%S",
                    PRINT_STR(CLI_SHARED->FLIOutputPath),
                    PRINT_STR(StrSkipLastSlash(FLIPath))
                )
            );
        }

        CLIFlight* Flight = CLIFlightFromFLIPath(FLIPath, TRUE);

        if (
            Flight->BINPath.Size &&
            !StrMatch(Flight->BINPath, Path, PathMatchKindFromSystem())
        ) {
            PrintErr(
                "%S and %S have the same FLI path, the second is skipped.\n",
                PRINT_STR(Flight->BINPath),
                PRINT_STR(Path)
            );
            ++CLI_SHARED->FailCount;
        } else if (!Flight->BINPath.Size) {
            Flight->BINPath = ArenaPushStrCpy(CLI_SHARED->MemPool, Path);
            Flight->BINSize = Props.Size;
            Flight->BINTime = MAX(Props.Created, Props.Modified);
        }
    } else if (StrMatch(Ext, "fli"_s8, STR_MATCH_ALL_CASES)) {
        CLIFlight* Flight = CLIFlightFromFLIPath(Path, AllocIfMissing);

        if (Flight) {
            Flight->HasFLI = TRUE;
            Flight->FLISize = Props.Size;
            Flight->FLITime = Props.Modified;
        }
    } else if (StrMatch(Ext, "flv"_s8, STR_MATCH_ALL_CASES)) {
        CLIScript* Script = NULL;

        for (
            CLIScript* S = CLI_SHARED->HeadScript;
            S;
            S = S->Next
        ) {
            if (StrMatch(S->Path, Path, PathMatchKindFromSystem())) {
                Script = S;
                break;
            }
        }

        if (!Script && AllocIfMissing) {
            Script = ArenaPushArrayZero(
                CLI_SHARED->MemPool, 
                CLIScript, 
                1
            );
            Script->Path = ArenaPushStrCpy(
                CLI_SHARED->MemPool,
                Path
            );
            Script->Time = Props.Modified;
            SLL_QUEUE_PUSH(
                CLI_SHARED->HeadScript,
                CLI_SHARED->TailScript,
                Script
            );
            ++CLI_SHARED->ScriptsCount;
        }
    } else {
        Result = FALSE;
    }

    ReleaseScratch(Scratch);

    return Result;
}

INTERNAL void
CLIGatherDir(Arena* MemPool, Str8List* Dirs, Str8 Dir, b32 AllocIfMissing)
{
    FileIter* Iter = SystemFileIterBegin(
        MemPool,
        Dir,
        SYS_FILE_ITER_SKIP_HIDDEN
    );

    for (
        FileInfo Info = {};
        SystemFileIterNext(MemPool, Iter, &Info);
    ) {
        Str8 Path = ArenaPushStrFmt(
            MemPool,
            "%S/%S",
            PRINT_STR(Dir),
            PRINT_STR(Info.Name)
        );

        if (Info.Properties.Kind & SYS_FILE_IS_DIR) {
            if (Dirs && CLI_SHARED->IsRecursive)
                ListPush(MemPool, Dirs, Path);
        } else {
            CLIPushFile(Path, Info.Properties, AllocIfMissing);
        }
    }

    SystemFileIterEnd(Iter);
}

// NOTE(nc): the larger flight is first. a BIN with no FLI yet is as large
//         : as its BIN.
INTERNAL i32
CLIFlightCmp(CLIFlight** A, CLIFlight** B)
{
    CLIFlight const* FlightA = *A;
    CLIFlight const* FlightB = *B;
    u64 SizeA = MAX(FlightA->BINSize, FlightA->FLISize);
    u64 SizeB = MAX(FlightB->BINSize, FlightB->FLISize);
    i32 Result = (i32) (SizeA < SizeB) - (i32) (SizeA > SizeB);

    return Result;
}

NC_DEFINE_QUICKSORT(CLIFlights, CLIFlight*, CLIFlightCmp)

INTERNAL void
CLIGather(CommandLine* CLI)
{
    TempArena Scratch = GetScratch(NULL, 0);
    Str8List Dirs = {};

    for (Str8Node* Node = CLI->Inputs.Head; Node; Node = Node->Next) {
        Str8 Path = PathNormalisedFromStr(Scratch.MemPool, Node->String);
        FileProperties Props = SystemGetFileProperties(Path);

        if (Props.Kind & SYS_FILE_IS_DIR) {
            ListPush(Scratch.MemPool, &Dirs, Path);
        } else if (!SystemFileExists(Path)) {
            PrintErr("%S was not found\n", PRINT_STR(Path));
            ++CLI_SHARED->FailCount;
        } else if (!CLIPushFile(Path, Props, TRUE)) {
            PrintErr("%S is not a BIN, FLI or FLV file\n", PRINT_STR(Path));
            ++CLI_SHARED->FailCount;
        }
    }

    for (Str8Node* Node = Dirs.Head; Node; Node = Node->Next)
        CLIGatherDir(Scratch.MemPool, &Dirs, Node->String, TRUE);

    if (CLI_SHARED->FLIOutputPath.Size)
        CLIGatherDir(Scratch.MemPool, NULL, CLI_SHARED->FLIOutputPath, FALSE);

    for (
        CLIFlight* Flight = CLI_SHARED->HeadFlight;
        Flight;
        Flight = Flight->Next
    ){
        if (Flight->BINPath.Size && !Flight->HasFLI) {
            FileProperties Props = SystemGetFileProperties(Flight->FLIPath);

            if (Props.Modified) {
                Flight->HasFLI = TRUE;
                Flight->FLISize = Props.Size;
                Flight->FLITime = Props.Modified;
            }
        }

        Flight->IsFLIStale = (
            Flight->BINPath.Size &&
            (
                CLI_SHARED->IsForced ||
                !Flight->HasFLI ||
                Flight->BINTime >= Flight->FLITime
            )
        );
    }

    CLI_SHARED->Flights = ArenaPushArray(
        CLI_SHARED->MemPool,
        CLIFlight*,
        CLI_SHARED->FlightsCount
    );

    {
        u64 Index = 0;

        for (
            CLIFlight* Flight = CLI_SHARED->HeadFlight;
            Flight;
            Flight = Flight->Next, ++Index
        ) {
            CLI_SHARED->Flights[Index] = Flight;
        }
    }

    QuickSortCLIFlights(CLI_SHARED->Flights, CLI_SHARED->FlightsCount);
    ReleaseScratch(Scratch);
}

INTERNAL void
CLIPrintGathered(void)
{
    u64 StaleCount = 0;

    for (
        CLIFlight* Flight = CLI_SHARED->HeadFlight; 
        Flight; 
        Flight = Flight->Next
    ) {
        StaleCount += !!Flight->IsFLIStale;

        if (!CLI_SHARED->IsDryRun)
            continue;

        PrintOut(
            "flight %S (%s)\n",
            PRINT_STR(Flight->FLIPath),
            Flight->IsFLIStale
                ? "to convert"
                : Flight->BINPath.Size
                    ? "up to date"
                    : "no BIN"
        );
    }

    if (CLI_SHARED->IsDryRun) {
        for (CLIScript* S = CLI_SHARED->HeadScript; S; S = S->Next) {
            PrintOut(
                "flv\t%S\n",
                PRINT_STR(S->Path)
            );
        }
    }

    PrintOut(
        "%llu flights (%llu to convert), %llu flv files\n",
        CLI_SHARED->FlightsCount,
        StaleCount,
        CLI_SHARED->ScriptsCount
    );
}

INTERNAL b32
CLIFLIHeaderIsCurrent(Str8 FLIPath)
{
    b32 Result = FALSE;
    Handle File = SystemOpenFile(
        SYS_ACCESS_READ | SYS_ACCESS_SHARE_READ,
        FLIPath
    );

    if (File != EMPTY_HANDLE_VALUE) {
        FLIHeader Header = {};
        u64 ReadSize = SystemReadStructFromFile(File, 0, &Header);

        Result = (
            ReadSize == sizeof(Header) &&
            Header.Magic == FLI_MAGIC_NUMBER &&
            Header.EncodingVersion == FLI_ENCODING_VERSION
        );
        SystemCloseFile(File);
    }

    return Result;
}

// NOTE(nc): every lane reads the headers of its part of the flights. an FLI
//         : of an old version is made again when it has a BIN.
INTERNAL void
CLICheckFLIHeaders(void)
{
    r1u64 Range = LaneRange(CLI_SHARED->FlightsCount);

    for (u64 Index = Range.Min; Index < Range.Max; ++Index) {
        CLIFlight* Flight = CLI_SHARED->Flights[Index];

        if (
            !Flight->HasFLI ||
            Flight->IsFLIStale ||
            CLIFLIHeaderIsCurrent(Flight->FLIPath)
        ) {
            continue;
        }

        if (Flight->BINPath.Size) {
            Flight->IsFLIStale = TRUE;
        } else {
            Flight->HasFailed = TRUE;
            AtomicIncFetchU64(&CLI_SHARED->FailCount, MEM_ORDER_SEQ_CST);
            PrintErr(
                "%S is not an FLI of this version and has no BIN\n",
                PRINT_STR(Flight->FLIPath)
            );
        }
    }
}

// NOTE(nc): a large file is mapped and a small one is read, as fli_main
//         : does. the result is NULL when the file has no data.
INTERNAL FLIFile*
CLIFileOpen(Arena* MemPool, Str8 Path)
{
    FLIFile* Result = NULL;
    Str8 Data = {};
    Handle FileHandle = {};
    Handle MapHandle = {};
    FileProperties Props = SystemGetFileProperties(Path);

    if (Props.Size > MB(100)) {
        Handle File = SystemOpenFile(
            SYS_ACCESS_READ | SYS_ACCESS_SHARE_READ,
            Path
        );

        if (File != EMPTY_HANDLE_VALUE) {
            Handle Map = SystemOpenFileMap(SYS_ACCESS_READ, File);

            if (Map != EMPTY_HANDLE_VALUE) {
                void* View = SystemOpenFileMapView(
                    Map,
                    SYS_ACCESS_READ,
                    Rng(0ULL, Props.Size)
                );

                if (View) {
                    Data = Str((u8*) View, Props.Size);
                    FileHandle = File;
                    MapHandle = Map;
                } else {
                    SystemCloseFileMap(Map);
                    SystemCloseFile(File);
                }
            } else {
                SystemCloseFile(File);
            }
        }
    }

    if (!Data.Size && Props.Size)
        Data = ReadFileContents(MemPool, Path);

    if (Data.Size) {
        Result = ArenaPushArrayZero(MemPool, FLIFile, 1);
        Result->Path = Path;
        Result->Data = Data;
        Result->FileHandle = FileHandle;
        Result->MapHandle = MapHandle;
    }

    return Result;
}

INTERNAL void
CLIFileClose(FLIFile* File)
{
    if (File->MapHandle != EMPTY_HANDLE_VALUE) {
        SystemCloseFileMapView(
            File->MapHandle,
            File->Data.Str,
            Rng(0ULL, File->Data.Size)
        );
        SystemCloseFileMap(File->MapHandle);
        SystemCloseFile(File->FileHandle);
    }
}

// NOTE(nc): all the lanes do one flight at a time. the bake has one shared
//         : state, so two flights are never in it together.
INTERNAL void
CLIConvertFlights(Arena* MemPool)
{
    if (
        !LaneIndex() &&
        CLI_SHARED->FLIOutputPath.Size &&
        !SystemDirExists(CLI_SHARED->FLIOutputPath)
    ) {
        SystemCreateDirectory(CLI_SHARED->FLIOutputPath);
    }

    for (u64 Index = 0; Index < CLI_SHARED->FlightsCount; ++Index) {
        CLIFlight* Flight = CLI_SHARED->Flights[Index];

        if (!Flight->IsFLIStale)
            continue;

        TempArena Scratch = GetScratch(&MemPool, 1);
        u64 Position = ArenaGetPosition(MemPool);
        FLIFile* BINFile = NULL;

        if (!LaneIndex()) {
            PrintOut("converting %S\n", PRINT_STR(Flight->BINPath));
            BINFile = CLIFileOpen(MemPool, Flight->BINPath);

            if (BINFile)
                BINFile->Format = FLI_FILE_FORMAT_KIND_BIN;
        }

        LaneSyncU64(&BINFile, 0);

        if (BINFile) {
            FLIBakeParams BakeParams = {};
            B2FConvertParams ConvertParams = {};

            ConvertParams.InputBINName = BINFile->Path;
            ConvertParams.InputBINData = BINFile->Data;
            BakeParams = B2FConvert(MemPool, &ConvertParams);
            LaneSync();

            FLIBakeResults BakeResults = {};

            BakeResults = FLIBake(MemPool, &BakeParams);
            LaneSync();

            FLISerialisedSectionBundle Bundle = {};

            if (!LaneIndex()) {
                Bundle = FLISerialisedSectionBundleFromBakeResults(
                    MemPool,
                    &BakeResults
                );
            }

            LaneSync();

            if (CLI_SHARED->DoCompress)
                Bundle = FLICompress(MemPool, &Bundle);

            LaneSync();

            if (!LaneIndex()) {
                Str8 TempPath = ArenaPushStrFmt(
                    Scratch.MemPool,
                    "%S.tmp",
                    PRINT_STR(Flight->FLIPath)
                );
                Str8List FileBlobs = FLIFileBlobsFromSectionBundle(
                    Scratch.MemPool,
                    &Bundle
                );
                b32 IsWritten = WriteFileContents(TempPath, FileBlobs);

                if (IsWritten)
                    IsWritten = SystemMoveFile(Flight->FLIPath, TempPath);

                if (IsWritten) {
                    FileProperties Props = SystemGetFileProperties(
                        Flight->FLIPath
                    );

                    Flight->HasFLI = TRUE;
                    Flight->FLISize = Props.Size;
                    Flight->FLITime = Props.Modified;
                    ++CLI_SHARED->ConvertedCount;
                } else {
                    SystemDeleteFile(TempPath);
                    Flight->HasFailed = TRUE;
                    ++CLI_SHARED->FailCount;
                    PrintErr(
                        "failed to write %S\n",
                        PRINT_STR(Flight->FLIPath)
                    );
                }

                CLIFileClose(BINFile);
            }

            LaneSync();
        } else if (!LaneIndex()) {
            Flight->HasFailed = TRUE;
            ++CLI_SHARED->FailCount;
            PrintErr("failed to read %S\n", PRINT_STR(Flight->BINPath));
        }

        ArenaPopTo(MemPool, Position);
        ReleaseScratch(Scratch);
    }
}

// NOTE(nc): every lane maps its part of the flights, as the load of the
//         : app does. the fixed sections are not packed, so a flight that
//         : is mapped is ready for the schema.
INTERNAL void
CLIOpenFlights(void)
{
    r1u64 Range = LaneRange(CLI_SHARED->FlightsCount);

    for (u64 Index = Range.Min; Index < Range.Max; ++Index) {
        CLIFlight* Flight = CLI_SHARED->Flights[Index];

        if (Flight->HasFailed || !Flight->HasFLI)
            continue;

        FLIParseStatus Status = FLI_PARSE_STATUS_HEADER_MISMATCH;

        Flight->File = SystemOpenFile(
            SYS_ACCESS_READ | SYS_ACCESS_SHARE_READ,
            Flight->FLIPath
        );

        if (Flight->File != EMPTY_HANDLE_VALUE) {
            FileProperties Props = SystemGetFileProperties(Flight->File);

            Flight->FileMap = SystemOpenFileMap(
                SYS_ACCESS_READ,
                Flight->File
            );

            if (Flight->FileMap != EMPTY_HANDLE_VALUE) {
                Flight->FileBase = SystemOpenFileMapView(
                    Flight->FileMap,
                    SYS_ACCESS_READ,
                    Rng(0ULL, Props.Size)
                );
            }

            if (Flight->FileBase) {
                Status = FLIParse(
                    (u8*) Flight->FileBase,
                    Props.Size,
                    &Flight->FLI
                );
            }
        }

        if (Status == FLI_PARSE_STATUS_SUCCESS) {
            Flight->Key.U64[0] = Hash(Flight->FLIPath);
            Flight->Key.U64[1] = Index + 1;
            Flight->IsOpen = TRUE;
        } else {
            Flight->HasFailed = TRUE;
            AtomicIncFetchU64(&CLI_SHARED->FailCount, MEM_ORDER_SEQ_CST);
            PrintErr("failed to open %S\n", PRINT_STR(Flight->FLIPath));
        }
    }
}

INTERNAL void
CLICompileScripts(void)
{
    TempArena Scratch = GetScratch(NULL, 0);
    FLIParsed** Flights = ArenaPushArray(
        Scratch.MemPool,
        FLIParsed*,
        CLI_SHARED->FlightsCount
    );
    u32 FlightsCount = 0;

    for (u64 Index = 0; Index < CLI_SHARED->FlightsCount; ++Index) {
        CLIFlight* Flight = CLI_SHARED->Flights[Index];

        if (Flight->IsOpen)
            Flights[FlightsCount++] = &Flight->FLI;
    }

    for (
        CLIScript* S = CLI_SHARED->HeadScript;
        S && FlightsCount;
        S = S->Next
    ) {
        S->Unit = FLVUnitFromFlights(
            S->Path,
            Flights,
            FlightsCount,
            CLI_SHARED->SearchPaths.Data,
            CLI_SHARED->SearchPaths.Count
        );

        if (!S->Unit) {
            ++CLI_SHARED->FailCount;
            PrintErr("failed to read %S\n", PRINT_STR(S->Path));
            continue;
        }

        for (Str8Node* N = S->Unit->Errors.Head; N; N = N->Next)
            PrintErr("%S\n", PRINT_STR(N->String));

        if (S->Unit->HasError) {
            ++CLI_SHARED->FailCount;
            PrintErr("%S has errors and is not run\n", PRINT_STR(S->Path));
        }

        // NOTE(nc): a result is as old as the newest file it is made from
        S->SourceTime = S->Time;

        for (u32 Index = 0; Index < S->Unit->ImportCount; ++Index) {
            FileProperties Props = SystemGetFileProperties(
                S->Unit->ImportPaths[Index]
            );

            S->SourceTime = MAX(S->SourceTime, Props.Modified);
        }

        if (!S->Unit->HasError)
            CLI_SHARED->ProgramsCount += S->Unit->Lowered.ProgramCount;
    }

    ReleaseScratch(Scratch);
}


// NOTE(nc): the emits of one directive are next to each other, with one
//         : kind and one group. the result is the index after the last.
INTERNAL u32
CLIDirectiveTail(FLVProgram const* P, u32 Head)
{
    u32 Result = Head + 1;

    while (
        Result < P->EmitCount &&
        P->Emits[Result].DisplayKind == P->Emits[Head].DisplayKind &&
        P->Emits[Result].Group == P->Emits[Head].Group
    ) {
        ++Result;
    }

    return Result;
}

INTERNAL Str8
CLIFileNameFromLabel(Arena* MemPool, Str8 Label)
{
    Str8 Result = ArenaPushStrCpy(
        MemPool,
        StrPrefix(Label, MIN(Label.Size, 48))
    );

    for (u64 Index = 0; Index < Result.Size; ++Index) {
        u8 Byte = Result.Str[Index];

        if (!IsAlpha(Byte) && !IsDigit(Byte) && Byte != '_' && Byte != '-')
            Result.Str[Index] = '_';
    }

    return Result;
}

INTERNAL Str8
CLIDirectivePath(
    Arena* MemPool,
    CLIFlight* Flight,
    CLIScript* Script,
    u32 Program,
    FLVProgram const* P,
    u32 Head,
    u32 Ordinal
) {
    Str8 Dir = CLI_SHARED->OutputPath;
    Str8 Kind = "graph"_s8;
    Str8 Label = ""_s8;
    Str8 Ext = "csv"_s8;

    if (
        P->Emits[Head].DisplayKind == TOKEN_KIND_GRAPH &&
        !CLI_SHARED->IsGraphCSV
    ) {
        Ext = "png"_s8;
    }

    if (!Dir.Size) {
        Dir = StrChopLastSlash(Flight->FLIPath);

        if (Dir.Size == Flight->FLIPath.Size)
            Dir = "."_s8;
    }

    if (P->Emits[Head].DisplayKind == TOKEN_KIND_TABLE) {
        Kind = "table"_s8;
    } else {
        if (P->Emits[Head].DisplayKind == TOKEN_KIND_FLAG)
            Kind = "flag"_s8;

        Label = CLIFileNameFromLabel(
            MemPool,
            P->Strings[P->Emits[Head].LabelStringIndex]
        );
    }

    Str8 Result = ArenaPushStrFmt(
        MemPool,
        "%S/%S.%S.p%u.d%u.%S%s%S.%S",
        PRINT_STR(Dir),
        PRINT_STR(StrChopLastDot(StrSkipLastSlash(Flight->FLIPath))),
        PRINT_STR(StrChopLastDot(StrSkipLastSlash(Script->Path))),
        Program,
        Ordinal,
        PRINT_STR(Kind),
        Label.Size ? "." : "",
        PRINT_STR(Label),
        PRINT_STR(Ext)
    );

    return Result;
}

// NOTE(nc): a program is fresh when all of its files are there and are
//         : newer than the flight and the FLV they are made from.
INTERNAL b32
CLIProgramIsFresh(
    Arena* MemPool,
    CLIFlight* Flight,
    CLIScript* Script,
    u32 Program,
    FLVProgram const* P
) {
    b32 Result = !CLI_SHARED->IsForced;
    u64 Position = ArenaGetPosition(MemPool);
    WallClockTime SourceTime = MAX(Flight->FLITime, Script->SourceTime);
    u32 Ordinal = 0;

    for (
        u32 Head = 0;
        Result && Head < P->EmitCount;
        Head = CLIDirectiveTail(P, Head), ++Ordinal
    ) {
        FileProperties Props = SystemGetFileProperties(
            CLIDirectivePath(
                MemPool,
                Flight,
                Script,
                Program,
                P,
                Head,
                Ordinal
            )
        );

        Result = (Props.Modified > SourceTime);
    }

    ArenaPopTo(MemPool, Position);

    return Result;
}

INTERNAL b32
CLIExportGraph(
    Arena* MemPool,
    FLAPExportWriter* Writer,
    Access* Acc,
    CLIFlight* Flight,
    FLIParsed* FLI,
    FLVProgram const* P,
    FLVRenderItem* Items,
    u32 Head,
    u32 Tail
) {
    FLAPExportWrite(Writer, "curve,time,value\n"_s8);

    for (u32 Index = Head; Index < Tail; ++Index) {
        FLVRenderItem const* Item = &Items[Index];

        if (Item->IsMissing || Item->Kind != FLV_RENDER_KIND_CURVE)
            continue;

        FLVRenderCurve const* Curve = &Item->Curve;
        Str8 Label = P->Strings[P->Emits[Index].LabelStringIndex];
        b32 IsRef = (Curve->ValueColumn != FLV_COLUMN_NONE);
        FLVColumn Time = {};
        FLVColumn Value = {};

        FLVColumnInit(
            Acc,
            Flight->Key,
            FLI,
            Curve->TimeColumn,
            U64_MAX,
            &Time
        );

        if (IsRef) {
            FLVColumnInit(
                Acc,
                Flight->Key,
                FLI,
                Curve->ValueColumn,
                U64_MAX,
                &Value
            );
        }

        if (Time.IsMissing)
            continue;

        b32 IsInteger = (
            IsRef &&
            Value.Scale == 1.0 &&
            Value.Kind != FLI_SCALAR_KIND_F32 &&
            Value.Kind != FLI_SCALAR_KIND_F64
        );
        Str8 Field = FLAPStrFromCSVField(MemPool, Label);
        u64 SeriesPosition = 0;
        u64 ValueIndex = 0;

        // NOTE(nc): the ranges are gone through in order, so the place in
        //         : the series and the place in the values only go up
        for (u64 R = 0; R < Curve->RangesCount; ++R) {
            for (
                u64 Sample = Curve->Ranges[R].Min;
                Sample < Curve->Ranges[R].Max;
                ++Sample, ++SeriesPosition
            ) {
                if (!FLVSeriesBit(Curve->Mask, SeriesPosition))
                    continue;

                u64 TimeUSecs = FLVTimeAt(&Time, Sample);
                f64 V = IsRef
                    ? FLVColumnF64At(&Value, Sample)
                    : (f64) Curve->Values[ValueIndex];
                u8* Ptr = FLAPExportReserve(
                    Writer,
                    Field.Size + FLAP_EXPORT_ROW_SIZE
                );

                ++ValueIndex;
                MemCpy(Ptr, Field.Str, Field.Size);
                Ptr += Field.Size;
                *Ptr++ = ',';
                Ptr = FLAPExportPutTime(Ptr, TimeUSecs);
                *Ptr++ = ',';

                // NOTE(nc): a value that is not a number has an empty
                //         : field. FLAPExportPutF64 writes nothing for it.
                if (IsInteger && V == V)
                    Ptr = FLAPExportPutI64(Ptr, (i64) V);
                else
                    Ptr = FLAPExportPutF64(Ptr, V);

                *Ptr++ = '\n';
                FLAPExportCommit(Writer, Ptr);
            }
        }
    }

    return TRUE;
}

INTERNAL b32
CLIExportFlag(
    Arena* MemPool,
    FLAPExportWriter* Writer,
    FLVRenderItem const* Item
) {
    FLAPExportWrite(Writer, "start,end,duration\n"_s8);

    if (!Item->IsMissing && Item->Kind == FLV_RENDER_KIND_FLAG) {
        u64 RowPosition = ArenaGetPosition(MemPool);

        for (u64 Index = 0; Index < Item->Flag.Count; ++Index) {
            r1u64 Range = Item->Flag.Ranges[Index];
            u64 Duration = Range.Max - Range.Min;

            FLAPExportWrite(
                Writer,
                ArenaPushStrFmt(
                    MemPool,
                    "%llu.%06llu,%llu.%06llu,%llu.%06llu\n",
                    Range.Min / MILLION(1),
                    Range.Min % MILLION(1),
                    Range.Max / MILLION(1),
                    Range.Max % MILLION(1),
                    Duration / MILLION(1),
                    Duration % MILLION(1)
                )
            );
            ArenaPopTo(MemPool, RowPosition);
        }
    }

    return TRUE;
}

// NOTE(nc): the table is written by the export of the app. a table that
//         : is missing has only its line of names.
INTERNAL b32
CLIExportTable(
    Arena* MemPool,
    FLAPExportWriter* Writer,
    Access* Acc,
    CLIFlight* Flight,
    FLIParsed* FLI,
    FLVProgram const* P,
    FLVRenderItem* Items,
    u32 Head,
    u32 Tail
) {
    b32 Result = TRUE;

    if (Items[Head].IsMissing || Items[Head].Kind != FLV_RENDER_KIND_TABLE) {
        FLAPExportWrite(Writer, "time"_s8);

        for (u32 Index = Head; Index < Tail; ++Index) {
            FLAPExportWrite(Writer, ","_s8);
            FLAPExportCSVField(
                Writer,
                P->Strings[P->Emits[Index].LabelStringIndex]
            );
        }

        FLAPExportWrite(Writer, "\n"_s8);
    } else {
        FLAPTableParams Params = {};
        FLAPExportSource Source = {};

        Params.Acc = Acc;
        Params.FLI = FLI;
        Params.Program = P;
        Params.Items = Items;
        Params.Flight = Flight->Key;
        Params.HeadEmit = Head;
        Source.Table = &Params;
        Result = FLAP_EXPORT_FUNC_NAME(CSV)(MemPool, &Source, Writer);
    }

    return Result;
}

// NOTE(nc): the flags and the modes of the draw data, as FLAPGraph has
//         : them, each for its switch. the flags are all the flags of the
//         : program, since a graph directive has none of its own. a flag
//         : takes its colour from the far end of the colours of the
//         : curves, and it is in the legend after the curves.
INTERNAL void
CLIGraphFlagsAndModes(
    Arena* MemPool,
    FLIParsed* FLI,
    FLVProgram const* P,
    FLVRenderItem* Items,
    FLAPGraphDrawData* Data
) {
    if (CLI_SHARED->HasGraphFlags) {
        Data->Flags = ArenaPushArrayZero(
            MemPool,
            FLVRenderFlag const*,
            P->EmitCount
        );
        Data->FlagColours = ArenaPushArrayZero(
            MemPool,
            v4f32,
            P->EmitCount
        );

        for (u32 Index = 0; Index < P->EmitCount; ++Index) {
            if (
                P->Emits[Index].DisplayKind != TOKEN_KIND_FLAG ||
                Items[Index].IsMissing ||
                Items[Index].Kind != FLV_RENDER_KIND_FLAG
            ) {
                continue;
            }

            FLAPGraphDrawLabel* Label = &Data->Labels[Data->LabelsCount++];
            v4f32 Colour = FLAPRasterVecFromColour(
                FLAP_RASTER_CURVE_COLOUR_TABLE[
                    (FLAP_RASTER_CURVE_COLOURS - 1) -
                    (Data->FlagsCount % FLAP_RASTER_CURVE_COLOURS)
                ]
            );

            Data->Flags[Data->FlagsCount] = &Items[Index].Flag;
            Data->FlagColours[Data->FlagsCount] = Colour;
            Label->String = P->Strings[P->Emits[Index].LabelStringIndex];
            Label->Colour = Colour;
            ++Data->FlagsCount;
        }
    }

    // NOTE(nc): the mode records of the flight, and the table of mode
    //         : names of its vehicle
    if (CLI_SHARED->HasGraphModes) {
        u64 ModesCount = 0;
        u64 InfoCount = 0;
        FLIMode* ModeTable = FLITableFromName(
            FLI,
            Modes,
            MODES,
            &ModesCount
        );
        FLIFileInfo* Infos = FLITableFromName(
            FLI,
            FileInfo,
            FILE_INFO,
            &InfoCount
        );
        FLVVehicleKind VehicleKind = FLV_VEHICLE_KIND_UNKNOWN;
        v4f32* ModeColours = ArenaPushArrayZero(
            MemPool,
            v4f32,
            FLAP_GRAPH_MODE_COLOURS
        );

        for (u32 Index = 0; Index < FLAP_GRAPH_MODE_COLOURS; ++Index) {
            ModeColours[Index] = FLAPRasterVecFromColour(
                FLAP_RASTER_CURVE_COLOUR_TABLE[
                    Index % FLAP_RASTER_CURVE_COLOURS
                ]
            );
        }

        if (InfoCount) {
            u64 Length = 0;
            u8* Base = FLIStrFromIndex(
                FLI,
                Infos[InfoCount - 1].VehicleStringIndex,
                &Length
            );

            VehicleKind = FLVVehicleKindFromStr(Str(Base, Length));
        }

        if (VehicleKind < FLV_VEHICLE_KIND_COUNT) {
            Data->ModeNames = FLV_MODE_TABLE[VehicleKind];
            Data->ModeNamesCount = FLV_MODE_TABLE_COUNT[VehicleKind];
        }

        // NOTE(nc): the first element of a table of the FLI is its null
        if (ModesCount > 1) {
            Data->Modes = ModeTable + 1;
            Data->ModesCount = (u32) (ModesCount - 1);
        }

        Data->ModeColours = ModeColours;
    }
}

// NOTE(nc): the draw data of the graph of one directive, as FLAPGraph
//         : makes it for its box. the time range is the fit of the curves
//         : and the value axes are the fit of the data in it. there is no
//         : cursor, no hold and no clock. the result is FALSE when a
//         : column is not ready.
INTERNAL b32
CLIGraphDataFromDirective(
    Arena* MemPool,
    Access* Acc,
    CLIFlight* Flight,
    FLIParsed* FLI,
    FLVProgram const* P,
    FLVRenderItem* Items,
    u32 Head,
    u32 Tail,
    v2i32 Size,
    OUT FLAPGraphDrawData* Data
) {
    b32 Result = TRUE;
    FLAPGraphMetrics Metrics = FLAPGraphMetricsFromFontSize(
        CLI_GRAPH_FONT_SIZE
    );
    u32 Width = (u32) Size.X;
    u32 Height = (u32) Size.Y;
    u32 Gutter = (u32) Metrics.Gutter;
    u32 Gutters = Gutter;
    u32 RowHeight = (u32) CLI_GRAPH_ROW_HEIGHT;
    r1u64 Fit = {};

    MemZero(Data, sizeof(*Data));
    Data->Curves = ArenaPushArrayZero(
        MemPool,
        FLAPGraphDrawCurve,
        Tail - Head
    );
    Data->Labels = ArenaPushArrayZero(
        MemPool,
        FLAPGraphDrawLabel,
        (Tail - Head) + P->EmitCount
    );
    Data->Metrics = Metrics;
    Data->LabelColour = FLAPRasterVecFromColour(FLAP_RASTER_COLOUR_LABEL);
    Fit.Min = U64_MAX;

    // NOTE(nc): the columns are waited for here, so the reduce finds them
    //         : ready. the fit is the time of all the curves.
    for (u32 Index = Head; Index < Tail; ++Index) {
        if (!FLAPGraphEmitIsCurve(P, Items, Index))
            continue;

        FLVRenderCurve const* Curve = &Items[Index].Curve;
        FLVColumn Time = {};
        FLVColumn Value = {};

        if (Curve->Axis == 2)
            Gutters = 2 * Gutter;

        Result &= FLVColumnInit(
            Acc,
            Flight->Key,
            FLI,
            Curve->TimeColumn,
            U64_MAX,
            &Time
        );

        if (Curve->ValueColumn != FLV_COLUMN_NONE) {
            Result &= FLVColumnInit(
                Acc,
                Flight->Key,
                FLI,
                Curve->ValueColumn,
                U64_MAX,
                &Value
            );
        }

        if (Time.IsMissing)
            continue;

        for (
            u64 RangeIndex = 0;
            RangeIndex < Curve->RangesCount;
            ++RangeIndex
        ) {
            r1u64 Range = Curve->Ranges[RangeIndex];

            if (Range.Min == Range.Max)
                continue;

            Fit.Min = MIN(Fit.Min, FLVTimeAt(&Time, Range.Min));
            Fit.Max = MAX(Fit.Max, FLVTimeAt(&Time, Range.Max - 1) + 1);
        }
    }

    Data->PlotX = Gutter;
    Data->PlotY = RowHeight;
    Data->Width = (Width > Gutters)
        ? Width - Gutters
        : 0;
    Data->Height = (Height > 2 * RowHeight)
        ? Height - 2 * RowHeight
        : 0;

    if (Fit.Min < Fit.Max)
        Data->TimeRangeUSecs = Fit;

    u64 Step = FLAPGraphStepFromSpan(
        Length(Data->TimeRangeUSecs),
        Data->Width * Metrics.ColumnsPerUnit
    );
    r1u64 View = FLAPGraphViewFromTimeRange(Data->TimeRangeUSecs, Step);
    // NOTE(nc): one column more at each side, as the app has, so that the
    //         : curve does not stop at the edge of the plot
    r1u64 Reduce = Rng(View.Min - (View.Min ? 1 : 0), View.Max + 1);

    Data->StepUSecs = Step;
    Data->View = View;

    for (u32 Index = Head; Index < Tail; ++Index) {
        FLAPGraphDrawLabel* Label = &Data->Labels[Data->LabelsCount++];
        Str8 Name = P->Strings[P->Emits[Index].LabelStringIndex];

        if (!FLAPGraphEmitIsCurve(P, Items, Index)) {
            Label->String = ArenaPushStrFmt(
                MemPool,
                "%S (missing)",
                PRINT_STR(Name)
            );
            Label->Colour = Data->LabelColour;
            continue;
        }

        FLAPGraphColumns* Columns = ArenaPushArrayZero(
            MemPool,
            FLAPGraphColumns,
            1
        );
        FLAPGraphDrawCurve* Curve = &Data->Curves[Data->CurvesCount];
        FLAPGraphKey Key = {};

        Key.Curve = &Items[Index].Curve;
        Key.Flight = Flight->Key;
        Key.StepUSecs = Step;
        Result &= FLAPGraphColumnsFromKey(
            MemPool,
            Columns,
            &Key,
            Step ? Reduce : Rng((u64) 0ULL, (u64) 0ULL),
            Acc,
            FLI
        );

        Curve->Columns = Columns;
        Curve->Name = Name;
        Curve->Axis = (Key.Curve->Axis == 2);
        Curve->Colour = FLAPRasterVecFromColour(
            FLAP_RASTER_CURVE_COLOUR_TABLE[
                Data->CurvesCount % FLAP_RASTER_CURVE_COLOURS
            ]
        );
        Label->Colour = Curve->Colour;
        Curve->Samples = FLAPGraphFitValueRange(
            Columns,
            View,
            Data->TimeRangeUSecs,
            &Data->ValueRanges[Curve->Axis],
            &Data->HasRange[Curve->Axis]
        );
        Label->String = ArenaPushStrFmt(
            MemPool,
            "%S%S",
            PRINT_STR(Name),
            PRINT_STR(Curve->Axis ? ":2"_s8 : ""_s8)
        );
        ++Data->CurvesCount;
    }

    CLIGraphFlagsAndModes(MemPool, FLI, P, Items, Data);

    for (u32 Axis = 0; Axis < 2; ++Axis) {
        if (Data->ValueRanges[Axis].Max <= Data->ValueRanges[Axis].Min) {
            Data->ValueRanges[Axis].Min -= 1.0f;
            Data->ValueRanges[Axis].Max += 1.0f;
        }
    }

    return Result;
}

INTERNAL void
CLIGraphEdgeLine(
    FLAPRasterMask* Mask,
    r2f32 Rect,
    f64 FromX,
    f64 FromY,
    f64 ToX,
    f64 ToY,
    f32 LineWidth
) {
    if (
        FLAPGraphClipLine(
            Rect,
            2.0 * (f64) LineWidth,
            &FromX,
            &FromY,
            &ToX,
            &ToY
        )
    ) {
        FLAPRasterLine(
            Mask,
            Vec((f32) FromX, (f32) FromY),
            Vec((f32) ToX, (f32) ToY),
            0.5f * LineWidth
        );
    }
}

// NOTE(nc): the coverage of one curve, in the mask. it is the curve loop
//         : of FLAPGraphDraw, with the shapes of the raster in the place
//         : of the shapes of the CAD pass. the positions are from the
//         : corner of the plot. there is no hover ring.
INTERNAL void
CLIGraphCurve(
    FLAPRasterMask* Mask,
    FLAPGraphDrawData const* Data,
    FLAPGraphDrawCurve const* Curve
) {
    FLAPGraphColumns const* Columns = Curve->Columns;
    FLAPGraphMetrics Metrics = Data->Metrics;
    r2f32 PlotRect = Rng(
        0.0f,
        0.0f,
        (f32) Data->Width,
        (f32) Data->Height
    );
    r1u64 View = Data->View;
    r1u64 ViewRange = Data->TimeRangeUSecs;
    u64 Step = Data->StepUSecs;
    f64 PXPerUSec = (f64) Data->Width / (f64) Length(ViewRange);
    f32 ColumnPX = (f32) ((f64) Step * PXPerUSec);
    f32 HalfColumnPX = 0.5f * ColumnPX;
    f32 HalfWidth = 0.5f * Metrics.LineWidth;
    u64 ColumnMin = View.Min - (
        View.Min
            ? 1
            : 0
    );
    u64 ColumnMax = View.Max + 1;
    r1f32 Range = Data->ValueRanges[Curve->Axis];
    f32 Scale = (f32) (Data->Height - 1) / (Range.Max - Range.Min);
    b32 HasMarkers = (
        Curve->Samples &&
        (f32) Curve->Samples * Metrics.MarkerSpacing <= (f32) Data->Width
    );
    b32 HasPrev = FALSE;
    b32 HasPoint = FALSE;
    b32 IsPrevEdge = FALSE;
    b32 IsPrevRange = FALSE;
    u64 PrevColumn = 0;
    f64 PrevEdgeX = 0.0;
    f32 PrevX = 0.0f;
    f32 PrevTop = 0.0f;
    f32 PrevBottom = 0.0f;
    f32 PrevLast = 0.0f;

    if (Columns->Lead.Has) {
        PrevEdgeX = (
            (f64) Columns->Lead.TimeUSecs - (f64) ViewRange.Min
        ) * PXPerUSec;
        PrevLast = (Range.Max - Columns->Lead.Value) * Scale + 0.5f;
        HasPrev = TRUE;
        IsPrevEdge = TRUE;
    }

    for (u64 Column = ColumnMin; Column < ColumnMax; ++Column) {
        u64 Slot = 0;

        if (!FLAPGraphColumnHas(Columns, Column, &Slot))
            continue;

        b32 IsRange = (Columns->Samples[Slot] != 1);
        f32 Offset = IsRange
            ? 0.5f
            : Columns->Phase[Slot];
        f32 X = (f32) (
            (f64) (
                (i64) (Column * Step) - (i64) ViewRange.Min
            ) * PXPerUSec
        ) + Offset * ColumnPX;
        f32 Top = (Range.Max - Columns->Max[Slot]) * Scale + 0.5f;
        f32 Bottom = (Range.Max - Columns->Min[Slot]) * Scale + 0.5f;
        f32 First = (Range.Max - Columns->First[Slot]) * Scale + 0.5f;
        f32 Last = (Range.Max - Columns->Last[Slot]) * Scale + 0.5f;
        b32 IsJoined = (
            HasPrev &&
            (Columns->Join[Slot >> 6] & (1ULL << (Slot & 63))) != 0
        );
        b32 IsAdjacent = (
            IsJoined &&
            !IsPrevEdge &&
            Column == PrevColumn + 1
        );

        if (HasPoint && !IsJoined)
            FLAPRasterDisc(Mask, Vec(PrevX, PrevTop), HalfWidth);

        if (IsPrevRange && !IsAdjacent) {
            FLAPRasterRibbon(
                Mask,
                Vec(PrevX, PrevTop),
                Vec(PrevX + HalfColumnPX, PrevTop),
                PrevBottom,
                PrevBottom,
                HalfWidth
            );
        }

        if (IsAdjacent) {
            FLAPRasterRibbon(
                Mask,
                Vec(PrevX, PrevTop),
                Vec(X, Top),
                PrevBottom,
                Bottom,
                HalfWidth
            );
        } else if (IsJoined && IsPrevEdge) {
            CLIGraphEdgeLine(
                Mask,
                PlotRect,
                PrevEdgeX,
                (f64) PrevLast,
                (f64) X,
                (f64) First,
                Metrics.LineWidth
            );
        } else if (IsJoined) {
            FLAPRasterLine(
                Mask,
                Vec(PrevX, PrevLast),
                Vec(X, First),
                HalfWidth
            );
        }

        if (IsRange && !IsAdjacent) {
            FLAPRasterRibbon(
                Mask,
                Vec(X - HalfColumnPX, Top),
                Vec(X, Top),
                Bottom,
                Bottom,
                HalfWidth
            );
        }

        if (HasMarkers)
            FLAPRasterDisc(Mask, Vec(X, Top), 2.0f * Metrics.LineWidth);

        HasPoint = (!IsRange && !IsJoined && !HasMarkers);
        HasPrev = TRUE;
        IsPrevEdge = FALSE;
        IsPrevRange = IsRange;
        PrevColumn = Column;
        PrevX = X;
        PrevTop = Top;
        PrevBottom = Bottom;
        PrevLast = Last;
    }

    if (IsPrevRange) {
        FLAPRasterRibbon(
            Mask,
            Vec(PrevX, PrevTop),
            Vec(PrevX + HalfColumnPX, PrevTop),
            PrevBottom,
            PrevBottom,
            HalfWidth
        );
    }

    if (HasPrev && Columns->Trail.Has) {
        f64 TrailX = (
            (f64) Columns->Trail.TimeUSecs - (f64) ViewRange.Min
        ) * PXPerUSec;
        f32 TrailY = (Range.Max - Columns->Trail.Value) * Scale + 0.5f;

        CLIGraphEdgeLine(
            Mask,
            PlotRect,
            IsPrevEdge
                ? PrevEdgeX
                : (f64) PrevX,
            (f64) PrevLast,
            TrailX,
            (f64) TrailY,
            Metrics.LineWidth
        );
    } else if (HasPoint) {
        FLAPRasterDisc(Mask, Vec(PrevX, PrevTop), HalfWidth);
    }
}

// NOTE(nc): the ticks of the graph, as FLAPGraphDraw has them for its
//         : labels. the time is from the start of the file, so the first
//         : tick is the first whole step in the view.
INTERNAL CLIGraphTicks
CLIGraphTicksFromData(FLAPGraphDrawData const* Data)
{
    CLIGraphTicks Result = {};
    FLAPGraphMetrics Metrics = Data->Metrics;
    r1u64 ViewRange = Data->TimeRangeUSecs;
    u64 Span = Length(ViewRange);

    for (u32 Axis = 0; Axis < 2; ++Axis) {
        if (!Data->HasRange[Axis] || !Data->Height)
            continue;

        r1f32 Range = Data->ValueRanges[Axis];
        f32 ValueStep = FLAPGraphTickStep(
            Range.Max - Range.Min,
            MAX(
                (u32) ((f32) Data->Height / MAX(Metrics.ValueTick, 1.0f)),
                (u32) 2
            )
        );

        Result.ValueSteps[Axis] = ValueStep;
        Result.ValueFirsts[Axis] = CeilF32(Range.Min / ValueStep);
    }

    if (Data->StepUSecs && Span && Data->Width) {
        u32 MaxTicks = MAX(
            (u32) ((f32) Data->Width / MAX(Metrics.TimeTick, 1.0f)),
            (u32) 1
        );
        u64 TickUSecs = FLAPGraphTickUSecs(Span, MaxTicks, FALSE);
        u64 First = ViewRange.Min - (ViewRange.Min % TickUSecs);

        if (First < ViewRange.Min)
            First += TickUSecs;

        Result.TimeStepUSecs = TickUSecs;
        Result.TimeFirstUSecs = First;
        Result.TimeDigits = (TickUSecs >= MILLION(1))
            ? 0
            : (TickUSecs >= THOUSAND(1))
                ? 3
                : 6;
    }

    return Result;
}

// NOTE(nc): one line for each tick, in the colour of the labels with a
//         : low alpha. it is drawn before the curves, so they are over it.
//         : the value lines are for the left axis, and for the right axis
//         : only when the left has no curve.
INTERNAL void
CLIGraphGrid(
    FLAPRasterImage* Image,
    FLAPGraphDrawData const* Data,
    CLIGraphTicks const* Ticks,
    r2i32 Plot
) {
    u32 Pixel = FLAPRasterPixelFromColour(FLAP_RASTER_COLOUR_LABEL);
    u32 Axis = Data->HasRange[0]
        ? 0
        : 1;
    r1u64 ViewRange = Data->TimeRangeUSecs;

    if (Ticks->ValueSteps[Axis] > 0.0f) {
        r1f32 Range = Data->ValueRanges[Axis];
        f32 Scale = (f32) (Data->Height - 1) / (Range.Max - Range.Min);

        for (u32 Tick = 0; ; ++Tick) {
            f32 Value = (
                (Ticks->ValueFirsts[Axis] + (f32) Tick) *
                Ticks->ValueSteps[Axis]
            );

            if (Value > Range.Max)
                break;

            // NOTE(nc): the row that a curve at this value has its middle
            //         : in
            i32 Y = Plot.Y0 + (i32) FloorF32(
                (Range.Max - Value) * Scale + 0.5f
            );

            FLAPRasterBlend(
                Image,
                Rng(Plot.X0, Y, Plot.X1, Y + 1),
                Pixel,
                CLI_GRAPH_GRID_ALPHA
            );
        }
    }

    if (Ticks->TimeStepUSecs) {
        f64 PXPerUSec = (f64) Data->Width / (f64) Length(ViewRange);

        for (
            u64 TimeUSecs = Ticks->TimeFirstUSecs;
            TimeUSecs < ViewRange.Max;
            TimeUSecs += Ticks->TimeStepUSecs
        ) {
            i32 X = Plot.X0 + (i32) (
                (f64) (TimeUSecs - ViewRange.Min) * PXPerUSec
            );

            FLAPRasterBlend(
                Image,
                Rng(X, Plot.Y0, X + 1, Plot.Y1),
                Pixel,
                CLI_GRAPH_GRID_ALPHA
            );
        }
    }
}

// NOTE(nc): the glyphs of the labels, made one time by the first lane
//         : before the lanes draw. the font is the main font of the app,
//         : from its bytes in the binary, or the file that was given.
//         : with no font the graphs are written with no text.
INTERNAL void
CLIGraphFontInit(void)
{
    FontProviderInit();

    // NOTE(nc): the provider keeps the pointer to the bytes, and they are
    //         : in the binary, so they are there for as long as the font
    FPHandle Handle = CLI_SHARED->GraphFontPath.Size
        ? OpenFont(CLI_SHARED->GraphFontPath)
        : FontOpen((Str8*) &APP_DEFAULT_MAIN_FONT_BYTES);

    if (Handle != EMPTY_FPHANDLE_VALUE) {
        CLI_SHARED->GraphFont = FLAPRasterFontFromHandle(
            CLI_SHARED->MemPool,
            Handle,
            CLI_GRAPH_FONT_SIZE
        );
        CloseFont(Handle);
    } else {
        PrintErr("the font was not opened, so the graphs have no text\n");
    }
}

// NOTE(nc): the legend, the value labels and the time labels, where
//         : FLAPGraphDraw has them. the box of the app is the image here.
//         : the time is from the start of the file.
INTERNAL void
CLIGraphLabels(
    Arena* MemPool,
    FLAPRasterImage* Image,
    FLAPRasterFont const* Font,
    FLAPGraphDrawData const* Data,
    CLIGraphTicks const* Ticks,
    r2i32 Plot
) {
    f32 FontSize = Font->Size;
    u32 LabelPixel = FLAPGraphMinimapPixel(Data->LabelColour);
    f32 LegendX = FontSize;

    for (
        u32 Index = 0;
        Index < Data->LabelsCount && LegendX < (f32) Image->Width;
        ++Index
    ) {
        FLAPGraphDrawLabel* Label = &Data->Labels[Index];

        FLAPRasterText(
            Image,
            Font,
            Vec(LegendX, 0.0f),
            Label->String,
            FLAPGraphMinimapPixel(Label->Colour)
        );
        LegendX += (
            FLAPRasterTextWidth(Font, Label->String) + 2.0f * FontSize
        );
    }

    // NOTE(nc): value labels, left axis then right axis
    for (u32 Axis = 0; Axis < 2; ++Axis) {
        f32 ValueStep = Ticks->ValueSteps[Axis];

        if (ValueStep <= 0.0f)
            continue;

        r1f32 Range = Data->ValueRanges[Axis];
        f32 Scale = (f32) (Data->Height - 1) / (Range.Max - Range.Min);
        char* Fmt = (char*) (
            (ValueStep >= 0.95f)
                ? "%.0f"
                : (ValueStep >= 0.095f)
                    ? "%.1f"
                    : (ValueStep >= 0.0095f)
                        ? "%.2f"
                        : "%.3f"
        );

        for (u32 Tick = 0; ; ++Tick) {
            f32 Value = (Ticks->ValueFirsts[Axis] + (f32) Tick) * ValueStep;

            if (Value > Range.Max)
                break;

            Str8 String = ArenaPushStrFmt(MemPool, Fmt, (f64) Value);
            f32 X = Axis
                ? (f32) Plot.X1 + FontSize
                : (f32) Plot.X0 - FontSize - FLAPRasterTextWidth(Font, String);
            f32 Y = (
                (f32) Plot.Y0 +
                (Range.Max - Value) * Scale -
                0.5f * FontSize
            );

            FLAPRasterText(Image, Font, Vec(X, Y), String, LabelPixel);
        }
    }

    if (Ticks->TimeStepUSecs) {
        r1u64 ViewRange = Data->TimeRangeUSecs;
        f64 PXPerUSec = (f64) Data->Width / (f64) Length(ViewRange);

        for (
            u64 TimeUSecs = Ticks->TimeFirstUSecs;
            TimeUSecs < ViewRange.Max;
            TimeUSecs += Ticks->TimeStepUSecs
        ) {
            f32 X = FloorF32(
                (f32) Plot.X0 + (f32) (
                    (f64) (TimeUSecs - ViewRange.Min) * PXPerUSec
                )
            );

            FLAPRasterText(
                Image,
                Font,
                Vec(X, (f32) Plot.Y1),
                FLAPGraphStrFromTime(
                    MemPool,
                    NULL,
                    TimeUSecs,
                    Ticks->TimeDigits,
                    FALSE
                ),
                LabelPixel
            );
        }
    }
}

// NOTE(nc): the modes and the flags, where FLAPGraphDraw has them. a mode
//         : is a tint of the plot from its record to the next one, with
//         : its name at the top of a span that has the room for it. a flag
//         : is a lane at the bottom edge of the plot, the first flag at
//         : the bottom. both are drawn before the curves. with no font
//         : the modes have no names.
INTERNAL void
CLIGraphModesAndFlags(
    Arena* MemPool,
    FLAPRasterImage* Image,
    FLAPRasterFont const* Font,
    FLAPGraphDrawData const* Data,
    r2i32 Plot
) {
    r1u64 ViewRange = Data->TimeRangeUSecs;
    u32 Width = Data->Width;
    f64 PXPerUSec = (f64) Width / (f64) Length(ViewRange);
    u32 LabelPixel = FLAPRasterPixelFromColour(FLAP_RASTER_COLOUR_LABEL);

    for (u32 Index = 0; Index < Data->ModesCount; ++Index) {
        u64 Start = Data->Modes[Index].TimeUSecs;
        u64 End = (Index + 1 < Data->ModesCount)
            ? Data->Modes[Index + 1].TimeUSecs
            : U64_MAX;

        if (End <= ViewRange.Min)
            continue;

        if (Start >= ViewRange.Max)
            break;

        i32 SpanX0 = Plot.X0 + (i32) (
            (f64) (MAX(Start, ViewRange.Min) - ViewRange.Min) * PXPerUSec
        );
        i32 SpanX1 = Plot.X0 + (i32) (
            (f64) (MIN(End, ViewRange.Max) - ViewRange.Min) * PXPerUSec
        );
        u32 Number = Data->Modes[Index].Mode;
        v4f32 Colour = Data->ModeColours[Number % FLAP_GRAPH_MODE_COLOURS];

        FLAPRasterBlend(
            Image,
            Rng(SpanX0, Plot.Y0, SpanX1, Plot.Y1),
            FLAPGraphMinimapPixel(Colour),
            0.15f * Colour.W
        );

        if (!Font)
            continue;

        Str8 Name = FLAPGraphModeName(
            MemPool,
            Data->ModeNames,
            Data->ModeNamesCount,
            Number
        );
        f32 NameWidth = FLAPRasterTextWidth(Font, Name);

        if ((f32) (SpanX1 - SpanX0) >= NameWidth + Font->Size) {
            FLAPRasterText(
                Image,
                Font,
                Vec((f32) SpanX0 + 0.5f * Font->Size, (f32) Plot.Y0),
                Name,
                LabelPixel
            );
        }
    }

    // NOTE(nc): the ranges of a flag that share a pixel are one rect
    if (Data->FlagsCount) {
        u8* Bands = ArenaPushArray(MemPool, u8, Width);
        i32 Lane = (i32) MAX(3.0f, FloorF32(0.4f * CLI_GRAPH_FONT_SIZE));

        for (u32 FlagIndex = 0; FlagIndex < Data->FlagsCount; ++FlagIndex) {
            FLVRenderFlag const* Flag = Data->Flags[FlagIndex];
            i32 LaneY1 = Plot.Y1 - (i32) FlagIndex * Lane;
            u32 Pixel = FLAPGraphMinimapPixel(Data->FlagColours[FlagIndex]);

            // NOTE(nc): no room for more lanes
            if (LaneY1 - Lane < Plot.Y0)
                break;

            MemZero(Bands, Width);

            for (u64 Index = 0; Index < Flag->Count; ++Index) {
                r1u64 Range = Flag->Ranges[Index];

                if (Range.Max <= ViewRange.Min || Range.Min >= ViewRange.Max)
                    continue;

                u32 First = MIN(
                    (u32) (
                        (f64) (
                            MAX(Range.Min, ViewRange.Min) - ViewRange.Min
                        ) * PXPerUSec
                    ),
                    Width - 1
                );
                u32 Last = MIN(
                    (u32) (
                        (f64) (
                            MIN(Range.Max, ViewRange.Max) - 1 - ViewRange.Min
                        ) * PXPerUSec
                    ),
                    Width - 1
                );

                for (u32 Band = First; Band <= Last; ++Band)
                    Bands[Band] = 1;
            }

            for (u32 Band = 0; Band < Width; ) {
                if (!Bands[Band]) {
                    ++Band;
                    continue;
                }

                u32 Start = Band;

                for ( ; Band < Width && Bands[Band]; ++Band);

                FLAPRasterFill(
                    Image,
                    Rng(
                        Plot.X0 + (i32) Start,
                        LaneY1 - Lane,
                        Plot.X0 + (i32) Band,
                        LaneY1
                    ),
                    Pixel
                );
            }
        }
    }
}

// NOTE(nc): the graph as a picture: the background, the plot, and each
//         : curve as a mask that is blended over them in its colour.
INTERNAL b32
CLIExportGraphImage(
    Arena* MemPool,
    FLAPExportWriter* Writer,
    FLAPRasterCanvas* Canvas,
    Access* Acc,
    CLIFlight* Flight,
    FLIParsed* FLI,
    FLVProgram const* P,
    FLVRenderItem* Items,
    u32 Head,
    u32 Tail
) {
    FLAPRasterImage* Image = &Canvas->Image;
    FLAPRasterMask* Mask = &Canvas->Mask;
    FLAPGraphDrawData Data = {};
    FLAPExportSource Source = {};
    i32 Width = (i32) Image->Width;
    i32 Height = (i32) Image->Height;
    b32 Result = CLIGraphDataFromDirective(
        MemPool,
        Acc,
        Flight,
        FLI,
        P,
        Items,
        Head,
        Tail,
        Vec(Width, Height),
        &Data
    );
    r2i32 Plot = Rng(
        (i32) Data.PlotX,
        (i32) Data.PlotY,
        (i32) (Data.PlotX + Data.Width),
        (i32) (Data.PlotY + Data.Height)
    );
    b32 HasPlot = (Data.Width && Data.Height && Data.StepUSecs);

    FLAPRasterFill(
        Image,
        Rng(0, 0, Width, Height),
        FLAPRasterPixelFromColour(FLAP_RASTER_COLOUR_BACKGROUND)
    );
    FLAPRasterFill(
        Image,
        Plot,
        FLAPRasterPixelFromColour(FLAP_RASTER_COLOUR_PLOT)
    );

    CLIGraphTicks Ticks = CLIGraphTicksFromData(&Data);

    if (HasPlot && CLI_SHARED->HasGraphGrid)
        CLIGraphGrid(Image, &Data, &Ticks, Plot);

    if (HasPlot) {
        CLIGraphModesAndFlags(
            MemPool,
            Image,
            CLI_SHARED->GraphFont,
            &Data,
            Plot
        );
    }

    // NOTE(nc): the mask is the plot, so a curve is cut at its edges
    Mask->Width = Data.Width;
    Mask->Height = Data.Height;

    for (
        u32 CurveIndex = 0;
        HasPlot && CurveIndex < Data.CurvesCount;
        ++CurveIndex
    ) {
        FLAPRasterMaskClear(Mask);
        CLIGraphCurve(Mask, &Data, &Data.Curves[CurveIndex]);
        FLAPRasterBlendMask(
            Image,
            Mask,
            Vec(Plot.X0, Plot.Y0),
            FLAPRasterPixelFromColour(
                FLAP_RASTER_CURVE_COLOUR_TABLE[
                    CurveIndex % FLAP_RASTER_CURVE_COLOURS
                ]
            )
        );
    }

    if (CLI_SHARED->GraphFont) {
        CLIGraphLabels(
            MemPool,
            Image,
            CLI_SHARED->GraphFont,
            &Data,
            &Ticks,
            Plot
        );
    }

    Source.Pixels = (u8*) Image->Pixels;
    Source.Size = Vec(Width, Height);
    Result = (
        FLAP_EXPORT_FUNC_NAME(PNG)(MemPool, &Source, Writer) &&
        Result
    );

    return Result;
}

// NOTE(nc): the file is made with another name and is moved to its name
//         : when it is whole, so a run that is stopped leaves no result
//         : that looks fresh.
INTERNAL b32
CLIExportDirective(
    Arena* MemPool,
    u8* Chunk,
    FLAPRasterCanvas* Canvas,
    Access* Acc,
    CLIFlight* Flight,
    FLIParsed* FLI,
    FLVProgram const* P,
    FLVRenderItem* Items,
    u32 Head,
    u32 Tail,
    Str8 FilePath
) {
    b32 Result = FALSE;
    u64 Position = ArenaGetPosition(MemPool);
    Str8 TempPath = ArenaPushStrFmt(MemPool, "%S.tmp", PRINT_STR(FilePath));
    FLAPExportWriter Writer = {};

    Writer.File = SystemOpenFile(SYS_ACCESS_WRITE, TempPath);

    if (Writer.File != EMPTY_HANDLE_VALUE) {
        Writer.Capacity = FLAP_EXPORT_CHUNK_SIZE;
        Writer.Chunk = Chunk;

        switch (P->Emits[Head].DisplayKind) {
            default: {} break;

            case TOKEN_KIND_GRAPH: {
                if (CLI_SHARED->IsGraphCSV) {
                    Result = CLIExportGraph(
                        MemPool,
                        &Writer,
                        Acc,
                        Flight,
                        FLI,
                        P,
                        Items,
                        Head,
                        Tail
                    );
                } else {
                    Result = CLIExportGraphImage(
                        MemPool,
                        &Writer,
                        Canvas,
                        Acc,
                        Flight,
                        FLI,
                        P,
                        Items,
                        Head,
                        Tail
                    );
                }
            } break;

            case TOKEN_KIND_FLAG: {
                Result = CLIExportFlag(MemPool, &Writer, &Items[Head]);
            } break;

            case TOKEN_KIND_TABLE: {
                Result = CLIExportTable(
                    MemPool,
                    &Writer,
                    Acc,
                    Flight,
                    FLI,
                    P,
                    Items,
                    Head,
                    Tail
                );
            } break;
        }

        FLAPExportFlush(&Writer);
        Result = (Result && !Writer.HasFailed);
        SystemCloseFile(Writer.File);

        if (Result)
            Result = SystemMoveFile(FilePath, TempPath);

        if (!Result)
            SystemDeleteFile(TempPath);
    }

    ArenaPopTo(MemPool, Position);

    return Result;
}

INTERNAL void
CLIMarkSections(
    u64* Needed,
    FLIParsed* FLI,
    FLVBoundSlot const* Bound,
    u32 SlotsCount
) {
    for (u32 Index = 0; Index < SlotsCount; ++Index) {
        FLVBoundSlot const* Slot = &Bound[Index];
        u32 Indices[3] = {
            Slot->Column,
            Slot->TimeColumn,
            Slot->InstanceColumn
        };

        if (Slot->IsMissing)
            continue;

        for (u32 I = 0; I < ARRAY_COUNT(Indices); ++I) {
            if (Indices[I] == FLV_COLUMN_NONE)
                continue;

            u64 Section = FLIElementFromNameIndex(
                FLI,
                Columns,
                COLUMNS,
                Indices[I]
            )->SectionIndex;

            if (Section < FLI->SectionsCount)
                Needed[Section >> 6] |= 1ULL << (Section & 63);
        }
    }
}

// NOTE(nc): the flight as it is mapped, with the packed sections that are
//         : needed unpacked into the arena. a section that is not packed
//         : is read from the map, and a packed section that is not needed
//         : has no data.
INTERNAL FLIParsed
CLIFlightUnpack(Arena* MemPool, CLIFlight* Flight, u64 const* Needed)
{
    FLIParsed* Src = &Flight->FLI;
    FLIParsed Result = *Src;

    Result.Unpacked = ArenaPushArrayZero(MemPool, u8*, Src->SectionsCount);

    for (u64 Index = 0; Index < Src->SectionsCount; ++Index) {
        FLISection* S = &Src->Sections[Index];

        if (
            !FLVMaskBit(Needed, Index) ||
            S->Encoding != FLI_SECTION_ENCODING_LZB ||
            !S->EncodedSize ||
            S->Offset + S->EncodedSize > Src->RawDataSize
        ) {
            continue;
        }

        u8* Data = (u8*) ArenaPushSize(MemPool, S->UnpackedSize, 8, FALSE);

        rr_lzb_simple_decode(
            Src->RawData + S->Offset,
            S->EncodedSize,
            Data,
            S->UnpackedSize
        );
        Result.Unpacked[Index] = Data;
    }

    return Result;
}
// NOTE(nc): a lane takes one flight. it looks first at which programs are
//         : fresh, and a flight with none to run is not unpacked. the arena
//         : goes back to where it was after the flight.
INTERNAL void
CLIRunFlights(Arena* MemPool)
{
    // NOTE(nc): the run has its own arena, as it has in the app. the
    //         : results go to the arena of the lane.
    Arena* RunMemPool = ArenaAlloc();
    u8* Chunk = ArenaPushArray(MemPool, u8, FLAP_EXPORT_CHUNK_SIZE);
    FLAPRasterCanvas Canvas = {};

    if (!CLI_SHARED->IsGraphCSV) {
        Canvas = FLAPRasterCanvasAlloc(
            MemPool,
            (u32) CLI_SHARED->GraphSize.X,
            (u32) CLI_SHARED->GraphSize.Y
        );
    }

    for (;;) {
        u64 Index = AtomicIncFetchU64(
            &CLI_SHARED->FlightTakeCounter,
            MEM_ORDER_SEQ_CST
        ) - 1;

        if (Index >= CLI_SHARED->FlightsCount)
            break;

        CLIFlight* Flight = CLI_SHARED->Flights[Index];

        if (!Flight->IsOpen)
            continue;

        u64 FlightPosition = ArenaGetPosition(MemPool);
        b32* IsFresh = ArenaPushArrayZero(
            MemPool,
            b32,
            CLI_SHARED->ProgramsCount
        );
        u64 Slot = 0;
        u64 FreshCount = 0;

        for (CLIScript* S = CLI_SHARED->HeadScript; S; S = S->Next) {
            FLVUnit* Unit = S->Unit;

            if (!Unit || Unit->HasError)
                continue;

            for (
                u32 Program = 0;
                Program < Unit->Lowered.ProgramCount;
                ++Program, ++Slot
            ) {
                IsFresh[Slot] = CLIProgramIsFresh(
                    MemPool,
                    Flight,
                    S,
                    Program,
                    &Unit->Lowered.Program[Program]
                );
                FreshCount += !!IsFresh[Slot];
            }
        }

        AtomicAddFetchU64(
            &CLI_SHARED->FreshCount,
            FreshCount,
            MEM_ORDER_SEQ_CST
        );

        if (FreshCount == CLI_SHARED->ProgramsCount) {
            ArenaPopTo(MemPool, FlightPosition);
            continue;
        }

        // NOTE(nc): the slots are bound on the flight as it is mapped. the
        //         : columns they name are the sections to unpack.
        FLVBoundSlot** Bounds = ArenaPushArrayZero(
            MemPool,
            FLVBoundSlot*,
            CLI_SHARED->ScriptsCount
        );
        u64* Needed = ArenaPushArrayZero(
            MemPool,
            u64,
            FLVSeriesWordCount(Flight->FLI.SectionsCount)
        );
        u64 ScriptIndex = 0;

        for (
            u64 Section = 0;
            Section < MIN(FLI_SECTION_KIND_COUNT, Flight->FLI.SectionsCount);
            ++Section
        ) {
            Needed[Section >> 6] |= 1ULL << (Section & 63);
        }

        Slot = 0;

        for (
            CLIScript* S = CLI_SHARED->HeadScript;
            S;
            S = S->Next, ++ScriptIndex
        ) {
            FLVUnit* Unit = S->Unit;
            b32 HasStale = FALSE;

            if (!Unit || Unit->HasError)
                continue;

            for (
                u32 Program = 0;
                Program < Unit->Lowered.ProgramCount;
                ++Program, ++Slot
            ) {
                HasStale |= !IsFresh[Slot];
            }

            if (!HasStale)
                continue;

            Bounds[ScriptIndex] = FLVBindSlots(
                MemPool,
                &Flight->FLI,
                Unit->Slots,
                Unit->SlotNames,
                Unit->SlotsCount
            );
            CLIMarkSections(
                Needed,
                &Flight->FLI,
                Bounds[ScriptIndex],
                Unit->SlotsCount
            );
        }

        FLIParsed FLI = CLIFlightUnpack(MemPool, Flight, Needed);

        Slot = 0;
        ScriptIndex = 0;

        for (
            CLIScript* S = CLI_SHARED->HeadScript;
            S;
            S = S->Next, ++ScriptIndex
        ) {
            FLVUnit* Unit = S->Unit;

            if (!Unit || Unit->HasError)
                continue;

            FLVBoundSlot* Bound = Bounds[ScriptIndex];

            // NOTE(nc): no slots were bound, so all its programs are fresh
            if (!Bound) {
                Slot += Unit->Lowered.ProgramCount;
                continue;
            }

            for (
                u32 Program = 0;
                Program < Unit->Lowered.ProgramCount;
                ++Program, ++Slot
            ) {
                if (IsFresh[Slot])
                    continue;

                u64 Position = ArenaGetPosition(MemPool);
                Access* Acc = AccessOpen();
                FLVProgram const* P = &Unit->Lowered.Program[Program];
                FLVLane Lane = {};
                u32 Ordinal = 0;

                FLVLaneInit(
                    &Lane,
                    Acc,
                    RunMemPool,
                    MemPool,
                    P,
                    Flight->Key,
                    &FLI,
                    Bound,
                    Unit->SlotsCount,
                    (u32) Index,
                    U64_MAX
                );
                FLVLaneRun(&Lane);

                for (u32 Head = 0; Head < P->EmitCount; ++Ordinal) {
                    u32 Tail = CLIDirectiveTail(P, Head);
                    Str8 Path = CLIDirectivePath(
                        MemPool,
                        Flight,
                        S,
                        Program,
                        P,
                        Head,
                        Ordinal
                    );
                    b32 IsWritten = CLIExportDirective(
                        MemPool,
                        Chunk,
                        &Canvas,
                        Acc,
                        Flight,
                        &FLI,
                        P,
                        Lane.Items,
                        Head,
                        Tail,
                        Path
                    );

                    if (IsWritten) {
                        AtomicIncFetchU64(
                            &CLI_SHARED->ExportedCount,
                            MEM_ORDER_SEQ_CST
                        );
                    } else {
                        AtomicIncFetchU64(
                            &CLI_SHARED->FailCount,
                            MEM_ORDER_SEQ_CST
                        );
                        PrintErr("failed to write %S\n", PRINT_STR(Path));
                    }

                    Head = Tail;
                }

                AccessClose(Acc);
                ArenaPopTo(MemPool, Position);
                ArenaClear(RunMemPool);
            }
        }

        ArenaPopTo(MemPool, FlightPosition);
    }

    ArenaRelease(RunMemPool);
}

void
CLIThreadEntryPoint(void* __Params)
{
    CLIThreadParams* Params = (CLIThreadParams*) __Params;
    CommandLine* CLI = Params->CLI;
    LaneContext LaneCtx = Params->LaneCtx;

    ThreadSetName("[CLI_%llu]", LaneCtx.Index);
    SetLaneContext(LaneCtx);

    Arena* MemPool = ArenaAlloc();

    if (!LaneIndex())
        CLIGather(CLI);

    LaneSync();
    CLICheckFLIHeaders();
    LaneSync();

    if (!LaneIndex())
        CLIPrintGathered();

    LaneSync();

    if (!CLI_SHARED->IsDryRun)
        CLIConvertFlights(MemPool);

    LaneSync();

    if (!CLI_SHARED->IsDryRun && CLI_SHARED->ScriptsCount) {
        CLIOpenFlights();
        LaneSync();

        if (!LaneIndex()) {
            CLICompileScripts();

            if (!CLI_SHARED->IsGraphCSV)
                CLIGraphFontInit();

            if (
                CLI_SHARED->OutputPath.Size &&
                !SystemDirExists(CLI_SHARED->OutputPath)
            ) {
                SystemCreateDirectory(CLI_SHARED->OutputPath);
            }
        }

        LaneSync();
        CLIRunFlights(MemPool);
        LaneSync();
    }

    if (!LaneIndex()) {
        PrintOut(
            "%llu converted, %llu files written, %llu programs fresh, %llu failed\n",
            CLI_SHARED->ConvertedCount,
            CLI_SHARED->ExportedCount,
            CLI_SHARED->FreshCount,
            CLI_SHARED->FailCount
        );
    }

    ArenaRelease(MemPool);
}

void
CLIEntryPoint(CommandLine* CLI)
{
    if (!CLI->Inputs.Count) {
        PrintErr(
            "FLAP CLI tool\n"
            "-------------------------------------------------------------------------------\n\n"_s8
            "USAGE\n"
            "-------------------------------------------------------------------------------\n"_s8
            "flap_cli [options] <files and folders>\n\n"
            "-------------------------------------------------------------------------------\n"_s8
            "BIN and FLI files are flights. FLV files are run on every flight.\n\n"
            "--out:<dir>         Where the results go. They go next to the\n"
            "                    flight when this is not given.\n\n"
            "--fli_out:<dir>     Where the FLI of a BIN goes. It goes next to\n"
            "                    the BIN when this is not given.\n\n"
            "--recursive         Folders inside the given folders are read.\n\n"
            "--force             All the work is done again.\n\n"
            "--dry_run           Lists what was found and stops.\n\n"
            "--thread_count:<n>  The number of worker threads.\n\n"_s8
            "--no_compress       The FLI files are written with no compression.\n\n"
            "--import:<dir>      A folder that FLV imports are looked for in.\n\n"
            "--graph_width:<n>   The width of a graph image, in pixels.\n\n"
            "--graph_height:<n>  The height of a graph image, in pixels.\n\n"
            "--graph_csv         A graph is written as CSV and not as PNG.\n\n"
            "--graph_grid        A graph image has a line at each tick.\n\n"
            "--graph_font:<file> The font of the text of a graph image. It\n"
            "                    is the font of the app when this is not given.\n\n"
            "--graph_modes       A graph image has the flight modes as a\n"
            "                    tint of its plot.\n\n"
            "--graph_flags       A graph image has the flags of its program\n"
            "                    as lanes at the bottom of its plot.\n"
        );

        return;
    }

    TempArena Scratch = GetScratch(NULL, 0);
    Arena* MemPool = ArenaAlloc();
    u64 ThreadsCount = GetSystemProperties()->LogicalProcessorCount;
    Str8 ThreadsCountRaw = CommandLineString(CLI, "thread_count"_s8);

    if (ThreadsCountRaw.Size) {
        u64 ThreadsCountFromCLI = U64FromStr(ThreadsCountRaw);

        if (ThreadsCountFromCLI)
            ThreadsCount = ThreadsCountFromCLI;
    }

    CLI_SHARED = ArenaPushArrayZero(MemPool, CLIShared, 1);
    CLI_SHARED->MemPool = MemPool;
    CLI_SHARED->FlightSlots = ArenaPushArrayZero(
        MemPool,
        CLIFlight*,
        CLI_FLIGHT_SLOTS_COUNT
    );

    Str8 OutputPath = CommandLineString(CLI, "out"_s8);
    Str8 FLIOutputPath = CommandLineString(CLI, "fli_out"_s8);
    Str8List SearchPaths = CommandLineStrings(CLI, "import"_s8);

    if (OutputPath.Size) {
        CLI_SHARED->OutputPath = PathNormalisedFromStr(
            MemPool,
            OutputPath
        );
    }

    if (FLIOutputPath.Size) {
        CLI_SHARED->FLIOutputPath = PathNormalisedFromStr(
            MemPool,
            FLIOutputPath
        );
    }

    CLI_SHARED->SearchPaths = StrArrayFromList(MemPool, &SearchPaths);
    CLI_SHARED->IsRecursive = CommandLineHasFlag(CLI, "recursive"_s8);
    CLI_SHARED->IsForced = CommandLineHasFlag(CLI, "force"_s8);
    CLI_SHARED->IsDryRun = CommandLineHasFlag(CLI, "dry_run"_s8);
    CLI_SHARED->DoCompress = !CommandLineHasFlag(CLI, "no_compress"_s8);
    CLI_SHARED->IsGraphCSV = CommandLineHasFlag(CLI, "graph_csv"_s8);
    CLI_SHARED->HasGraphGrid = CommandLineHasFlag(CLI, "graph_grid"_s8);
    CLI_SHARED->HasGraphModes = CommandLineHasFlag(CLI, "graph_modes"_s8);
    CLI_SHARED->HasGraphFlags = CommandLineHasFlag(CLI, "graph_flags"_s8);
    CLI_SHARED->GraphFontPath = CommandLineString(CLI, "graph_font"_s8);
    CLI_SHARED->GraphSize = Vec(
        (i32) CLI_GRAPH_WIDTH,
        (i32) CLI_GRAPH_HEIGHT
    );

    Str8 GraphWidthRaw = CommandLineString(CLI, "graph_width"_s8);
    Str8 GraphHeightRaw = CommandLineString(CLI, "graph_height"_s8);

    if (GraphWidthRaw.Size) {
        CLI_SHARED->GraphSize.X = (i32) CLAMP(
            (u64) CLI_GRAPH_SIZE_MIN,
            U64FromStr(GraphWidthRaw),
            (u64) CLI_GRAPH_SIZE_MAX
        );
    }

    if (GraphHeightRaw.Size) {
        CLI_SHARED->GraphSize.Y = (i32) CLAMP(
            (u64) CLI_GRAPH_SIZE_MIN,
            U64FromStr(GraphHeightRaw),
            (u64) CLI_GRAPH_SIZE_MAX
        );
    }

    Handle* Threads = ArenaPushArrayZero(
        Scratch.MemPool,
        Handle,
        ThreadsCount
    );
    CLIThreadParams* ThreadsParams = ArenaPushArrayZero(
        Scratch.MemPool,
        CLIThreadParams,
        ThreadsCount
    );
    Handle Barrier = BarrierAlloc(ThreadsCount);
    u64 BroadcastValue = 0;

    for (u64 Index = 0; Index < ThreadsCount; ++Index) {
        ThreadsParams[Index].CLI = CLI;
        ThreadsParams[Index].LaneCtx.Index = Index;
        ThreadsParams[Index].LaneCtx.Count = ThreadsCount;
        ThreadsParams[Index].LaneCtx.Barrier = Barrier;
        ThreadsParams[Index].LaneCtx.BroadcastMemory = &BroadcastValue;
        Threads[Index] = ThreadLaunch(
            CLIThreadEntryPoint,
            &ThreadsParams[Index]
        );
    }

    for (u64 Index = 0; Index < ThreadsCount; ++Index)
        ThreadJoin(Threads[Index], U64_MAX);

    ReleaseScratch(Scratch);
}
