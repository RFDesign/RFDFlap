#include "nc_arena.h"
#include "nc_atomics.h"
#include "nc_memory.h"
#include "nc_print.h"
#include "nc_sync.h"
#include "nc_simd.h"
#include "nc_sort.h"

#include "fli.h"
#include "fli_bin.h"
#include "fli_main.h"

#include "flv_bind.h"
#include "flv_compile.h"
#include "flv_run.h"
#include "flv_token.h"

#include "cli_main.h"

#include "flap_widgets.h"
#include "flap_export.h"

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
        "%S/%S.%S.p%u.d%u.%S%s%S.csv",
        PRINT_STR(Dir),
        PRINT_STR(StrChopLastDot(StrSkipLastSlash(Flight->FLIPath))),
        PRINT_STR(StrChopLastDot(StrSkipLastSlash(Script->Path))),
        Program,
        Ordinal,
        PRINT_STR(Kind),
        Label.Size ? "." : "",
        PRINT_STR(Label)
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

// NOTE(nc): the file is made with another name and is moved to its name
//         : when it is whole, so a run that is stopped leaves no result
//         : that looks fresh.
INTERNAL b32
CLIExportDirective(
    Arena* MemPool,
    u8* Chunk,
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
            "                    flight when this is not given.\n"
            "--fli_out:<dir>     Where the FLI of a BIN goes. It goes next to\n"
            "                    the BIN when this is not given.\n"
            "--recursive         Folders inside the given folders are read.\n"
            "--force             All the work is done again.\n"
            "--dry_run           Lists what was found and stops.\n"
            "--thread_count:<n>  The number of worker threads.\n"_s8
            "--no_compress       The FLI files are written with no compression.\n"
            "--import:<dir>      A folder that FLV imports are looked for in.\n"
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
