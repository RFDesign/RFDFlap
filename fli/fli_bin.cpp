#include "fli_bin.h"
#include "nc_memory.h"
#include "nc_atomics.h"
#include "nc_profile.h"
#include "nc_hash.h"
#include "nc_math.h"

global const B2FDerivedInfo B2F_DERIVED_INFO_TABLE[B2F_DERIVED_KIND_COUNT] = {
    {
        Str8Lit(""),
        Str8Lit(""),  
        FLI_SCALAR_KIND_NULL 
    },
    { 
        Str8Lit("N"),    
        Str8Lit("m"), 
        FLI_SCALAR_KIND_F32 
    },
    { 
        Str8Lit("E"),    
        Str8Lit("m"), 
        FLI_SCALAR_KIND_F32 
    },
    { 
        Str8Lit("Dist"), 
        Str8Lit("m"), 
        FLI_SCALAR_KIND_F64 
    },
    { 
        Str8Lit("Prev"), 
        Str8Lit(""),  
        FLI_SCALAR_KIND_F32 
    }
};

INTERNAL B2FFormatCharInfo
B2FFormatCharInfoFromChar(u8 C)
{
    B2FFormatCharInfo Result = {};

    Result.Scale = 1.0;

    switch (C) {
        default: {} break;

        case 'b': { 
            Result.Kind = FLI_SCALAR_KIND_I8;
            Result.Size = 1; 
        } break;

        case 'B': {
            Result.Kind = FLI_SCALAR_KIND_U8;
            Result.Size = 1; 
        } break;

        case 'M': {
            Result.Kind = FLI_SCALAR_KIND_U8;
            Result.Size = 1; 
        } break;

        case 'h': {
            Result.Kind = FLI_SCALAR_KIND_I16;
            Result.Size = 2; 
        } break;

        case 'H': {
            Result.Kind = FLI_SCALAR_KIND_U16;
            Result.Size = 2; 
        } break;

        case 'i': {
            Result.Kind = FLI_SCALAR_KIND_I32;
            Result.Size = 4; 
        } break;

        case 'I': {
            Result.Kind = FLI_SCALAR_KIND_U32;
            Result.Size = 4; 
        } break;

        case 'q': {
            Result.Kind = FLI_SCALAR_KIND_I64;
            Result.Size = 8; 
        } break;

        case 'Q': {
            Result.Kind = FLI_SCALAR_KIND_U64;
            Result.Size = 8; 
        } break;

        case 'f': {
            Result.Kind = FLI_SCALAR_KIND_F32;
            Result.Size = 4; 
        } break;

        case 'd': {
            Result.Kind = FLI_SCALAR_KIND_F64;
            Result.Size = 8; 
        } break;

        case 'n': {
            Result.Kind = FLI_SCALAR_KIND_CHAR4;
            Result.Size = 4; 
        } break;

        case 'N': {
            Result.Kind = FLI_SCALAR_KIND_CHAR16;
            Result.Size = 16; 
        } break;

        case 'Z': {
            Result.Kind = FLI_SCALAR_KIND_CHAR64;
            Result.Size = 64; 
        } break;

        case 'c': {
            Result.Kind = FLI_SCALAR_KIND_I16;
            Result.Size = 2;
            Result.Scale = 0.01; 
        } break;

        case 'C': {
            Result.Kind = FLI_SCALAR_KIND_U16;
            Result.Size = 2;
            Result.Scale = 0.01; 
        } break;

        case 'e': {
            Result.Kind = FLI_SCALAR_KIND_I32;
            Result.Size = 4;
            Result.Scale = 0.01; 
        } break;

        case 'E': {
            Result.Kind = FLI_SCALAR_KIND_U32;
            Result.Size = 4;
            Result.Scale = 0.01; 
        } break;

        case 'L': {
            Result.Kind = FLI_SCALAR_KIND_I32;
            Result.Size = 4;
            Result.Scale = 1.0e-7; 
        } break;
    }

    return Result;
}

INTERNAL b32
B2FScalarKindIsChar(FLIScalarKind Kind)
{
    return (
        Kind == FLI_SCALAR_KIND_CHAR4 ||
        Kind == FLI_SCALAR_KIND_CHAR16 ||
        Kind == FLI_SCALAR_KIND_CHAR64
    );
}

INTERNAL Str8
B2FStrFromFixedChars(u8* Chars, u64 Capacity)
{
    u64 Size = 0;

    for ( ; Size < Capacity && Chars[Size]; ++Size);

    return Str(Chars, Size);
}

INTERNAL f64
B2FF64FromScalar(u8* Ptr, FLIScalarKind Kind)
{
    f64 Result = 0.0;

    switch (Kind) {
        default: {} break;

        case FLI_SCALAR_KIND_I8: { 
            i8  V; 
            
            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_U8: { 
            u8  V; 
            
            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_I16: { 
            i16 V; 

            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_U16: { 
            u16 V; 

            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_I32: { 
            i32 V; 

            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_U32: { 
            u32 V; 

            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_I64: { 
            i64 V; 

            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_U64: { 
            u64 V; 

            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_F32: { 
            f32 V; 

            MemCpy(&V, Ptr, sizeof(V)); 
            Result = (f64) V; 
        } break;

        case FLI_SCALAR_KIND_F64: { 
            f64 V; 

            MemCpy(&V, Ptr, sizeof(V)); 
            Result = V; 
        } break;
    }

    return Result;
}

INTERNAL void
B2FScalarFromF64(u8* Ptr, FLIScalarKind Kind, f64 Value)
{
    switch (Kind) {
        default: {} break;

        case FLI_SCALAR_KIND_I8: { 
            i8  V = (i8) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_U8: { 
            u8  V = (u8) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_I16: { 
            i16 V = (i16) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_U16: { 
            u16 V = (u16) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_I32: { 
            i32 V = (i32) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_U32: { 
            u32 V = (u32) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_I64: { 
            i64 V = (i64) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_U64: { 
            u64 V = (u64) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_F32: { 
            f32 V = (f32) Value; 

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;

        case FLI_SCALAR_KIND_F64: { 
            f64 V = Value;

            MemCpy(Ptr, &V, sizeof(V)); 
        } break;
    }
}

INTERNAL u32
B2FFieldIndexFromName(B2FFormat* Format, Str8 Name)
{
    u32 Result = BIN_MAX_FIELD_COUNT;

    for (u32 Index = 0; Index < Format->FieldCount; ++Index) {
        if (StrMatch(Format->FieldNames[Index], Name, 0)) {
            Result = Index;
            break;
        }
    }

    return Result;
}

INTERNAL f64
B2FF64FromField(u8* Body, B2FFormat* Format, u32 FieldIndex)
{
    f64 Result = 0.0;

    if (FieldIndex < Format->FieldCount) {
        B2FFormatCharInfo Info = B2FFormatCharInfoFromChar(
            Format->FieldChars[FieldIndex]
        );

        Result = B2FF64FromScalar(
            Body + Format->FieldOffsets[FieldIndex], 
            Info.Kind
        ) * Info.Scale;
    }

    return Result;
}

INTERNAL u64
B2FU64FromField(u8* Body, B2FFormat* Format, u32 FieldIndex)
{
    u64 Result = 0;

    if (FieldIndex < Format->FieldCount) {
        B2FFormatCharInfo Info = B2FFormatCharInfoFromChar(
            Format->FieldChars[FieldIndex]
        );

        MemCpy(
            &Result, 
            Body + Format->FieldOffsets[FieldIndex], 
            MIN(Info.Size, sizeof(Result))
        );
    }

    return Result;
}

INTERNAL Str8
B2FStrFromField(u8* Body, B2FFormat* Format, u32 FieldIndex)
{
    Str8 Result = {};

    if (FieldIndex < Format->FieldCount) {
        B2FFormatCharInfo Info = B2FFormatCharInfoFromChar(
            Format->FieldChars[FieldIndex]
        );

        if (B2FScalarKindIsChar(Info.Kind)) {
            Result = B2FStrFromFixedChars(
                Body + Format->FieldOffsets[FieldIndex], 
                Info.Size
            );
        }
    }

    return Result;
}

INTERNAL void
B2FDerivedPush(B2FFormat* Format, B2FDerivedKind Kind)
{
    Str8 Name = B2F_DERIVED_INFO_TABLE[Kind].Name;

    if (
        Format->DerivedCount < B2F_DERIVED_MAX &&
        B2FFieldIndexFromName(Format, Name) == BIN_MAX_FIELD_COUNT
    ) {
        Format->DerivedKinds[Format->DerivedCount++] = Kind;
    }
}

INTERNAL void
B2FFormatFromFMTBody(Arena* MemPool, B2FFormat* Formats, u8* Body)
{
    u8 Type = Body[0];
    B2FFormat* Format = &Formats[Type];

    MemSet(Format, 0, sizeof(*Format));
    Format->Type = Type;
    Format->Length = Body[1];
    Format->Name = ArenaPushStrCpy(
        MemPool, 
        B2FStrFromFixedChars(Body + 2, 4)
    );
    Format->FormatString = ArenaPushStrCpy(
        MemPool, 
        B2FStrFromFixedChars(Body + 6, 16)
    );
    Format->Supported = TRUE;
    Format->TimeField = BIN_MAX_FIELD_COUNT;

    Str8 Columns = B2FStrFromFixedChars(Body + 22, 64);
    u8* Cursor = Columns.Str;
    u8* End = Columns.Str + Columns.Size;
    u32 Offset = 0;

    for (
        u32 Index = 0; 
        Index < Format->FormatString.Size && Index < BIN_MAX_FIELD_COUNT; 
        ++Index
    ) {
        u8* NameStart = Cursor;

        for ( ; Cursor < End && *Cursor != ','; ++Cursor);

        B2FFormatCharInfo Info = B2FFormatCharInfoFromChar(
            Format->FormatString.Str[Index]
        );

        Format->FieldNames[Index] = ArenaPushStrCpy(
            MemPool, 
            Str(NameStart, INT_FROM_PTR(Cursor - NameStart))
        );
        Format->FieldChars[Index] = Format->FormatString.Str[Index];
        Format->FieldOffsets[Index] = (u8) Offset;
        Format->UnitIds[Index] = '-';
        Format->MultIds[Index] = '-';
        Offset += Info.Size;
        ++Format->FieldCount;

        if (Info.Kind == FLI_SCALAR_KIND_NULL)
            Format->Supported = FALSE;

        if (Cursor < End)
            ++Cursor;
    }

    if (Format->Length != BIN_HEAD_SIZE + Offset)
        Format->Supported = FALSE;

    Format->TimeField = B2FFieldIndexFromName(Format, "TimeUS"_s8);
    Format->HasTime = (Format->TimeField < Format->FieldCount);

    // NOTE(nc): the GPS records have the wall clock of the log: the week
    //         : and the milliseconds of the week.
    if (StrMatch(Format->Name, "GPS"_s8, 0)) {
        Format->IsGPS = TRUE;
        Format->WeekField = B2FFieldIndexFromName(Format, "GWk"_s8);
        Format->MSecsField = B2FFieldIndexFromName(Format, "GMS"_s8);
        Format->StatusField = B2FFieldIndexFromName(Format, "Status"_s8);
        Format->HasClock = (
            Format->HasTime &&
            Format->WeekField < Format->FieldCount &&
            Format->MSecsField < Format->FieldCount
        );
    }

    if (StrMatch(Format->Name, "FMT"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_FMT;
    else if (StrMatch(Format->Name, "FMTU"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_FMTU;
    else if (StrMatch(Format->Name, "UNIT"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_UNIT;
    else if (StrMatch(Format->Name, "MULT"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_MULT;
    else if (StrMatch(Format->Name, "PARM"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_PARM;
    else if (StrMatch(Format->Name, "MSG"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_MSG;
    else if (StrMatch(Format->Name, "MODE"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_MODE;
    else if (StrMatch(Format->Name, "EV"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_EV;
    else if (StrMatch(Format->Name, "ERR"_s8, 0))
        Format->Special = B2F_SPECIAL_KIND_ERR;

    // NOTE(nc): a message with a Lat and a Lng in degrees has a position
    Format->LatField = B2FFieldIndexFromName(Format, "Lat"_s8);
    Format->LngField = B2FFieldIndexFromName(Format, "Lng"_s8);
    Format->CommandField = BIN_MAX_FIELD_COUNT;
    Format->NameField = B2FFieldIndexFromName(Format, "Name"_s8);
    Format->ValueField = B2FFieldIndexFromName(Format, "Value"_s8);
    Format->HasPosition = (
        Format->LatField < Format->FieldCount &&
        Format->LngField < Format->FieldCount &&
        Format->FieldChars[Format->LatField] == 'L' &&
        Format->FieldChars[Format->LngField] == 'L'
    );

    if (StrMatch(Format->Name, "CMD"_s8, 0))
        Format->CommandField = B2FFieldIndexFromName(Format, "CId"_s8);

    if (Format->HasPosition) {
        B2FDerivedPush(Format, B2F_DERIVED_KIND_NORTH);
        B2FDerivedPush(Format, B2F_DERIVED_KIND_EAST);
        B2FDerivedPush(Format, B2F_DERIVED_KIND_DIST);
    }

    if (
        Format->Special == B2F_SPECIAL_KIND_PARM &&
        Format->NameField < Format->FieldCount &&
        Format->ValueField < Format->FieldCount &&
        Format->FieldChars[Format->NameField] == 'N' &&
        Format->FieldChars[Format->ValueField] == 'f'
    ) {
        B2FDerivedPush(Format, B2F_DERIVED_KIND_PREV);
    }
}

INTERNAL void
B2FOffsetPush(Arena* MemPool, B2FFormat* Format, u64 Offset)
{
    B2FOffsetChunk* Chunk = Format->TailOffsets;

    if (!Chunk || Chunk->Count >= Chunk->Capacity) {
        Chunk = ArenaPushArrayZero(MemPool, B2FOffsetChunk, 1);
        Chunk->Capacity = 16384;
        Chunk->V = ArenaPushArray(MemPool, u64, Chunk->Capacity);
        SLL_QUEUE_PUSH(Format->HeadOffsets, Format->TailOffsets, Chunk);
    }

    Chunk->V[Chunk->Count] = Offset;
    ++Chunk->Count;
    ++Format->RecordCount;
}

struct B2FShared {
    B2FFormat*     Formats;
    B2FFormat**    Tasks;
    u64            TasksCount;
    u64            TaskTakeCounter;
    u64            StartTimeUS;
    MD5Hash        SourceMD5;
    FLIBakeParams* Result;
    f64            OriginLat;
    f64            OriginLng;
    f64            OriginCosLat;
    b32            HasOrigin;
    u32            __Padding;
};

// NOTE(nc): one position of an instance. a position of 0, 0 is a position
//         : that is not there, and the last one stays. the distance of a
//         : step has the latitude of the step, so it is true far from the
//         : origin. N and E are flat about the origin.
INTERNAL void
B2FPositionStep(
    B2FShared const* Shared,
    B2FPositionState* State,
    f64 Lat,
    f64 Lng
) {
    if (Lat == 0.0 && Lng == 0.0)
        return;

    if (State->HasPosition) {
        f64 MidLat = RADFromDEG(0.5 * (State->Lat + Lat));
        f64 DeltaNorth = RADFromDEG(Lat - State->Lat) * BIN_EARTH_RADIUS_METRES;
        f64 DeltaEast = (
            RADFromDEG(Lng - State->Lng) *
            BIN_EARTH_RADIUS_METRES *
            CosF64(MidLat)
        );

        State->Dist += SqrtF64(
            DeltaNorth * DeltaNorth + DeltaEast * DeltaEast
        );
    }

    State->Lat = Lat;
    State->Lng = Lng;
    State->HasPosition = TRUE;

    if (Shared->HasOrigin) {
        State->North = (
            RADFromDEG(Lat - Shared->OriginLat) * BIN_EARTH_RADIUS_METRES
        );
        State->East = (
            RADFromDEG(Lng - Shared->OriginLng) *
            BIN_EARTH_RADIUS_METRES *
            Shared->OriginCosLat
        );
    }
}

// NOTE(nc): the value that the parameter had before this record. the
//         : first record of a name has its own value, so a record is a
//         : change when its Value is not its Prev.
INTERNAL f32
B2FParamPrev(B2FParamSlot* Slots, u64 SlotsCount, Str8 Name, f32 Value)
{
    u8 Key[16] = {};
    f32 Result = Value;

    MemCpy(Key, Name.Str, MIN(Name.Size, sizeof(Key)));

    for (
        u64 Index = Hash(Key, sizeof(Key)) & (SlotsCount - 1);
        ;
        Index = (Index + 1) & (SlotsCount - 1)
    ) {
        B2FParamSlot* Slot = &Slots[Index];

        if (!Slot->IsUsed) {
            MemCpy(Slot->Name, Key, sizeof(Key));
            Slot->Value = Value;
            Slot->IsUsed = TRUE;
            break;
        }

        if (MemCmp(Slot->Name, Key, sizeof(Key))) {
            Result = Slot->Value;
            Slot->Value = Value;
            break;
        }
    }

    return Result;
}

FLIBakeParams
B2FConvert(Arena* MemPool, B2FConvertParams* Params)
{
    TempArena Scratch = GetScratch(&MemPool, 1);
    u8* FileData = Params->InputBINData.Str;
    u64 FileSize = Params->InputBINData.Size;
    B2FShared* Shared = NULL;
    const u64 MessageTypeCapacity = BIN_TYPE_COUNT;
    // NOTE(nc): the columns of a message are in one chunk
    const u64 FieldCapacity = BIN_TYPE_COUNT * (
        BIN_MAX_FIELD_COUNT + B2F_DERIVED_MAX
    );
    const u64 ColumnCapacity = BIN_TYPE_COUNT * (
        BIN_MAX_FIELD_COUNT + B2F_DERIVED_MAX
    );
    const u64 ParamCapacity = 4096;
    const u64 MessageCapacity = 1024;
    const u64 ModeCapacity = 256;
    const u64 EventCapacity = 1024;
    const u64 ErrorCapacity = 1024;

    if (!LaneIndex()) {
        Shared = ArenaPushArrayZero(Scratch.MemPool, B2FShared, 1);
        Shared->Formats = ArenaPushArrayZero(
            Scratch.MemPool, 
            B2FFormat, 
            BIN_TYPE_COUNT
        );
        Shared->Result = ArenaPushArrayZero(MemPool, FLIBakeParams, 1);
    }

    LaneSyncU64(&Shared, 0);

    B2FFormat* Formats = Shared->Formats;
    FLIBakeParams* Result = Shared->Result;

    if (LaneIndex() == LaneFromTaskIndex(1)) {
        PROFILE(B2FMD5Timer) {
            Shared->SourceMD5 = MD5FromStr(Params->InputBINData);
        }
    }

    if (!LaneIndex()) {
        PROFILE(B2FScanTimer) {
            Str8 UnitLabels[BIN_TYPE_COUNT] = {};
            f64 MultValues[BIN_TYPE_COUNT] = {};
            u8* Cursor = FileData;
            u8* End = FileData + FileSize;
            u64 RecordCount = 0;
            u64 SkippedRecordCount = 0;
            u64 MinTimeUSecs = U64_MAX;
            u64 MaxTimeUSecs = 0;
            u64 ClockUnixUSecs = 0;
            u64 ClockTimeUSecs = 0;
            f64 OriginLats[FLI_ORIGIN_KIND_COUNT] = {};
            f64 OriginLngs[FLI_ORIGIN_KIND_COUNT] = {};
            b32 HasOrigins[FLI_ORIGIN_KIND_COUNT] = {};

            for (u32 Index = 0; Index < BIN_TYPE_COUNT; ++Index)
                MultValues[Index] = 1.0;

            while (Cursor + BIN_HEAD_SIZE <= End) {
                if (
                    Cursor[0] != BIN_HEAD_BYTE_0 || 
                    Cursor[1] != BIN_HEAD_BYTE_1
                ) {
                    ++Cursor;
                    continue;
                }

                u8 Type = Cursor[2];

                if (Type == BIN_FMT_TYPE) {
                    if (Cursor + BIN_FMT_LENGTH > End)
                        break;

                    B2FFormatFromFMTBody(
                        MemPool, 
                        Formats, 
                        Cursor + BIN_HEAD_SIZE
                    );
                    Cursor += BIN_FMT_LENGTH;
                    ++RecordCount;
                    continue;
                }

                B2FFormat* Format = &Formats[Type];

                if (!Format->Length) {
                    ++Cursor;
                    continue;
                }

                if (Cursor + Format->Length > End)
                    break;

                u8* Body = Cursor + BIN_HEAD_SIZE;
                u64 TimeUSecs = 0;

                if (Format->HasTime) {
                    TimeUSecs = B2FU64FromField(Body, Format, Format->TimeField);
                    MinTimeUSecs = MIN(MinTimeUSecs, TimeUSecs);
                    MaxTimeUSecs = MAX(MaxTimeUSecs, TimeUSecs);
                }

                // NOTE(nc): the first GPS record with a fix is the clock
                //         : of the log. a status of 3 is a 3D fix, and a
                //         : log with no status takes the first week that
                //         : is not zero. GPS time is ahead of UTC by the
                //         : leap seconds.
                if (Format->HasClock && !ClockUnixUSecs) {
                    u64 Week = B2FU64FromField(
                        Body,
                        Format,
                        Format->WeekField
                    );
                    b32 HasFix = (
                        Format->StatusField >= Format->FieldCount ||
                        B2FU64FromField(
                            Body,
                            Format,
                            Format->StatusField
                        ) >= 3
                    );

                    if (Week && HasFix) {
                        ClockUnixUSecs = (
                            BIN_GPS_EPOCH_UNIX_SECS +
                            Week * BIN_GPS_WEEK_SECS -
                            BIN_GPS_LEAP_SECS
                        ) * MILLION(1) + B2FU64FromField(
                            Body,
                            Format,
                            Format->MSecsField
                        ) * THOUSAND(1);
                        ClockTimeUSecs = TimeUSecs;
                    }
                }

                // NOTE(nc): the origin of the positions: the first land
                //         : command of the mission, and the first 3D fix
                //         : for a log with none.
                if (Format->HasPosition) {
                    FLIOriginKind Kind = FLI_ORIGIN_KIND_NULL;

                    if (
                        Format->CommandField < Format->FieldCount &&
                        B2FU64FromField(
                            Body,
                            Format,
                            Format->CommandField
                        ) == BIN_CMD_ID_NAV_LAND
                    ) {
                        Kind = FLI_ORIGIN_KIND_LAND_COMMAND;
                    } else if (
                        Format->IsGPS &&
                        (
                            Format->StatusField >= Format->FieldCount ||
                            B2FU64FromField(
                                Body,
                                Format,
                                Format->StatusField
                            ) >= 3
                        )
                    ) {
                        Kind = FLI_ORIGIN_KIND_FIX;
                    }

                    if (Kind && !HasOrigins[Kind]) {
                        f64 Lat = B2FF64FromField(
                            Body,
                            Format,
                            Format->LatField
                        );
                        f64 Lng = B2FF64FromField(
                            Body,
                            Format,
                            Format->LngField
                        );

                        if (Lat != 0.0 || Lng != 0.0) {
                            OriginLats[Kind] = Lat;
                            OriginLngs[Kind] = Lng;
                            HasOrigins[Kind] = TRUE;
                        }
                    }
                }

                switch (Format->Special) {
                    default: {} break;

                    case B2F_SPECIAL_KIND_NONE: {
                        if (Format->Supported && Format->HasTime) {
                            B2FOffsetPush(
                                Scratch.MemPool, 
                                Format, 
                                INT_FROM_PTR(Cursor - FileData)
                            );
                        } else {
                            ++SkippedRecordCount;
                        }
                    } break;

                    case B2F_SPECIAL_KIND_FMTU: {
                        u8 FmtType = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "FmtType"_s8)
                        );
                        Str8 UnitIds = B2FStrFromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "UnitIds"_s8)
                        );
                        Str8 MultIds = B2FStrFromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "MultIds"_s8)
                        );

                        MemCpy(
                            Formats[FmtType].UnitIds, 
                            UnitIds.Str, 
                            MIN(UnitIds.Size, BIN_MAX_FIELD_COUNT)
                        );
                        MemCpy(
                            Formats[FmtType].MultIds, 
                            MultIds.Str, 
                            MIN(MultIds.Size, BIN_MAX_FIELD_COUNT)
                        );
                    } break;

                    case B2F_SPECIAL_KIND_UNIT: {
                        u8 Id = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Id"_s8)
                        );
                        Str8 Label = B2FStrFromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Label"_s8)
                        );

                        UnitLabels[Id] = ArenaPushStrCpy(MemPool, Label);
                    } break;

                    case B2F_SPECIAL_KIND_MULT: {
                        u8 Id = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Id"_s8)
                        );

                        MultValues[Id] = B2FF64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Mult"_s8)
                        );
                    } break;

                    case B2F_SPECIAL_KIND_PARM: {
                        FLIBakeParam* Param = FLIBakeParamChunkListPush(
                            MemPool, 
                            &Result->Params, 
                            ParamCapacity
                        );

                        Param->Name = ArenaPushStrCpy(
                            MemPool, 
                            B2FStrFromField(
                                Body, 
                                Format, 
                                B2FFieldIndexFromName(Format, "Name"_s8)
                            )
                        );
                        Param->Value = (f32) B2FF64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Value"_s8)
                        );
                        Param->Default = (f32) B2FF64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Default"_s8)
                        );
                        Param->TimeUSecs = TimeUSecs;

                        // NOTE(nc): the record is a row of the PARM
                        //         : columns too
                        if (Format->Supported && Format->HasTime) {
                            B2FOffsetPush(
                                Scratch.MemPool,
                                Format,
                                INT_FROM_PTR(Cursor - FileData)
                            );
                        }
                    } break;

                    case B2F_SPECIAL_KIND_MSG: {
                        FLIBakeMessage* Message = FLIBakeMessageChunkListPush(
                            MemPool, 
                            &Result->Messages, 
                            MessageCapacity
                        );

                        Message->Text = ArenaPushStrCpy(
                            MemPool, 
                            B2FStrFromField(
                                Body, 
                                Format, 
                                B2FFieldIndexFromName(Format, "Message"_s8)
                            )
                        );
                        Message->TimeUSecs = TimeUSecs;

                        if (
                            !Result->FileInfo.Firmware.Size &&
                            (
                                StrMatch(
                                    StrPrefix(Message->Text, 4), 
                                    "Ardu"_s8, 
                                    0
                                ) ||
                                StrMatch(
                                    StrPrefix(Message->Text, 7), 
                                    "Antenna"_s8, 
                                    0
                                ) ||
                                StrMatch(
                                    StrPrefix(Message->Text, 5), 
                                    "Blimp"_s8, 
                                    0
                                )
                            )
                        ) {
                            u64 SpaceIndex = 0;

                            for (
                                ;
                                (
                                    SpaceIndex < Message->Text.Size && 
                                    Message->Text.Str[SpaceIndex] != ' '
                                ); 
                                ++SpaceIndex
                            );

                            Result->FileInfo.Firmware = Message->Text;
                            Result->FileInfo.Vehicle = StrPrefix(
                                Message->Text, 
                                SpaceIndex
                            );
                        }

                        // NOTE(nc): the record is a row of the MSG
                        //         : columns too
                        if (Format->Supported && Format->HasTime) {
                            B2FOffsetPush(
                                Scratch.MemPool,
                                Format,
                                INT_FROM_PTR(Cursor - FileData)
                            );
                        }
                    } break;

                    case B2F_SPECIAL_KIND_MODE: {
                        FLIBakeMode* Mode = FLIBakeModeChunkListPush(
                            MemPool, 
                            &Result->Modes, 
                            ModeCapacity
                        );

                        Mode->TimeUSecs = TimeUSecs;
                        Mode->Mode = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Mode"_s8)
                        );
                        Mode->ModeNum = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "ModeNum"_s8)
                        );
                        Mode->Reason = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Rsn"_s8)
                        );
                    } break;

                    case B2F_SPECIAL_KIND_EV: {
                        FLIBakeEvent* Event = FLIBakeEventChunkListPush(
                            MemPool, 
                            &Result->Events, 
                            EventCapacity
                        );

                        Event->TimeUSecs = TimeUSecs;
                        Event->ID = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Id"_s8)
                        );
                    } break;

                    case B2F_SPECIAL_KIND_ERR: {
                        FLIBakeError* Error = FLIBakeErrorChunkListPush(
                            MemPool, 
                            &Result->Errors, 
                            ErrorCapacity
                        );

                        Error->TimeUSecs = TimeUSecs;
                        Error->Subsystem = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "Subsys"_s8)
                        );
                        Error->Code = (u8) B2FU64FromField(
                            Body, 
                            Format, 
                            B2FFieldIndexFromName(Format, "ECode"_s8)
                        );
                    } break;
                }

                Cursor += Format->Length;
                ++RecordCount;
            }

            if (MinTimeUSecs == U64_MAX)
                MinTimeUSecs = 0;

            Shared->StartTimeUS = MinTimeUSecs;
            Result->FileInfo.SourcePath = ArenaPushStrCpy(MemPool, Params->InputBINName);
            Result->FileInfo.StartTimeUSecs = MinTimeUSecs;
            Result->FileInfo.EndTimeUSecs = MaxTimeUSecs;
            Result->FileInfo.RecordCount = RecordCount;
            Result->FileInfo.SkippedRecordCount = SkippedRecordCount;
            Result->FileInfo.OriginKind = HasOrigins[FLI_ORIGIN_KIND_LAND_COMMAND]
                ? FLI_ORIGIN_KIND_LAND_COMMAND
                : HasOrigins[FLI_ORIGIN_KIND_FIX]
                    ? FLI_ORIGIN_KIND_FIX
                    : FLI_ORIGIN_KIND_NULL;
            Result->FileInfo.OriginLat = OriginLats[Result->FileInfo.OriginKind];
            Result->FileInfo.OriginLng = OriginLngs[Result->FileInfo.OriginKind];
            Shared->HasOrigin = (
                Result->FileInfo.OriginKind != FLI_ORIGIN_KIND_NULL
            );
            Shared->OriginLat = Result->FileInfo.OriginLat;
            Shared->OriginLng = Result->FileInfo.OriginLng;
            Shared->OriginCosLat = CosF64(RADFromDEG(Shared->OriginLat));

            // NOTE(nc): the times of the FLI are from the first record, so
            //         : the clock is moved back to it.
            if (ClockUnixUSecs) {
                Result->FileInfo.StartUnixUSecs = (
                    ClockUnixUSecs - (ClockTimeUSecs - MinTimeUSecs)
                );
            }

            for (
                FLIBakeParamChunkNode* Node = Result->Params.Head; 
                Node; 
                Node = Node->Next
            ) {
                for (u64 Index = 0; Index < Node->Count; ++Index)
                    Node->V[Index].TimeUSecs-= MinTimeUSecs;
            }

            for (
                FLIBakeMessageChunkNode* Node = Result->Messages.Head; 
                Node; 
                Node = Node->Next
            ) {
                for (u64 Index = 0; Index < Node->Count; ++Index)
                    Node->V[Index].TimeUSecs -= MinTimeUSecs;
            }

            for (
                FLIBakeModeChunkNode* Node = Result->Modes.Head; 
                Node; 
                Node = Node->Next
            ) {
                for (u64 Index = 0; Index < Node->Count; ++Index)
                    Node->V[Index].TimeUSecs -= MinTimeUSecs;
            }

            for (
                FLIBakeEventChunkNode* Node = Result->Events.Head; 
                Node; 
                Node = Node->Next
            ) {
                for (u64 Index = 0; Index < Node->Count; ++Index)
                    Node->V[Index].TimeUSecs -= MinTimeUSecs;
            }

            for (
                FLIBakeErrorChunkNode* Node = Result->Errors.Head; 
                Node; 
                Node = Node->Next
            ) {
                for (u64 Index = 0; Index < Node->Count; ++Index)
                    Node->V[Index].TimeUSecs -= MinTimeUSecs;
            }

            Shared->Tasks = ArenaPushArrayZero(
                Scratch.MemPool, 
                B2FFormat*, 
                BIN_TYPE_COUNT
            );

            for (u32 Type = 0; Type < BIN_TYPE_COUNT; ++Type) {
                B2FFormat* Format = &Formats[Type];

                if (!Format->RecordCount)
                    continue;

                FLIBakeMessageType* MessageType = FLIBakeMessageTypeChunkListPush(
                    MemPool, 
                    &Result->MessageTypes, 
                    MessageTypeCapacity
                );

                Format->MessageType = MessageType;
                MessageType->InstanceField = 0;
                MessageType->ID = Format->Type;
                MessageType->Length = Format->Length;
                MessageType->Name = Format->Name;
                MessageType->Format = Format->FormatString;
                MessageType->FieldCount = Format->FieldCount + Format->DerivedCount;
                MessageType->ColumnCount = Format->FieldCount + Format->DerivedCount;
                MessageType->RecordCount = Format->RecordCount;

                for (u32 Index = 0; Index < Format->FieldCount; ++Index) {
                    B2FFormatCharInfo Info = B2FFormatCharInfoFromChar(
                        Format->FieldChars[Index]
                    );
                    FLIBakeField* Field = FLIBakeFieldChunkListPush(
                        MemPool, 
                        &Result->Fields, 
                        FieldCapacity
                    );
                    FLIBakeColumn* Column = FLIBakeColumnChunkListPush(
                        MemPool, 
                        &Result->Columns, 
                        ColumnCapacity
                    );
                    u8 UnitId = Format->UnitIds[Index];
                    u8 MultId = Format->MultIds[Index];
                    f64 Scale = Info.Scale;

                    if (Scale == 1.0 && MultId != '-' && MultId != '?')
                        Scale = MultValues[MultId];

                    if (!Index) {
                        MessageType->HeadField = Field;
                        MessageType->HeadColumn = Column;
                    }

                    if (Index == Format->TimeField)
                        MessageType->TimeColumn = Column;

                    if (
                        !MessageType->InstanceField &&
                        (
                            UnitId == '#' ||
                            StrMatch(Format->FieldNames[Index], "I"_s8, 0) ||
                            StrMatch(Format->FieldNames[Index], "Instance"_s8, 0) ||
                            StrMatch(Format->FieldNames[Index], "Inst"_s8, 0)
                        )
                    ) {
                        MessageType->InstanceField = Index + 1;
                    }

                    Field->MessageType = MessageType;
                    Field->Name = Format->FieldNames[Index];
                    Field->Unit = UnitLabels[UnitId];
                    Field->FormatChar = Format->FieldChars[Index];
                    Field->LoggedKind = Info.Kind;
                    Field->LoggedSize = Info.Size;
                    Field->Multiplier = Scale;
                    Field->Column = Column;
                    Field->StoredKind = Info.Kind;
                    Column->MessageType = MessageType;
                    Column->Field = Field;
                    Column->Kind = Info.Kind;
                    Column->Encoding = FLI_COLUMN_ENCODING_RAW;
                    Column->Multiplier = Scale;
                    Column->Count = Format->RecordCount;
                    Column->DataSize = Column->Count * FLI_SCALAR_KIND_SIZE_TABLE[Column->Kind];
                    Column->Data = ArenaPushArray(
                        MemPool, 
                        u8, 
                        Column->DataSize
                    );
                }

                // NOTE(nc): the columns that are made, after the columns
                //         : of the log. a field of them has no logged kind.
                for (u32 Index = 0; Index < Format->DerivedCount; ++Index) {
                    B2FDerivedInfo const* Info = &B2F_DERIVED_INFO_TABLE[
                        Format->DerivedKinds[Index]
                    ];
                    FLIBakeField* Field = FLIBakeFieldChunkListPush(
                        MemPool, 
                        &Result->Fields, 
                        FieldCapacity
                    );
                    FLIBakeColumn* Column = FLIBakeColumnChunkListPush(
                        MemPool, 
                        &Result->Columns, 
                        ColumnCapacity
                    );

                    Field->MessageType = MessageType;
                    Field->Name = Info->Name;
                    Field->Unit = Info->Unit;
                    Field->FormatChar = BIN_DERIVED_FORMAT_CHAR;
                    Field->LoggedKind = FLI_SCALAR_KIND_NULL;
                    Field->LoggedSize = 0;
                    Field->Multiplier = 1.0;
                    Field->Column = Column;
                    Field->StoredKind = Info->Kind;
                    Column->MessageType = MessageType;
                    Column->Field = Field;
                    Column->Kind = Info->Kind;
                    Column->Encoding = FLI_COLUMN_ENCODING_RAW;
                    Column->Multiplier = 1.0;
                    Column->Count = Format->RecordCount;
                    Column->DataSize = Column->Count * FLI_SCALAR_KIND_SIZE_TABLE[Column->Kind];
                    Column->Data = ArenaPushArray(
                        MemPool, 
                        u8, 
                        Column->DataSize
                    );
                }

                u64 Insert = Shared->TasksCount++;

                for (
                    ; 
                    (
                        Insert && 
                        Shared->Tasks[Insert - 1]->RecordCount < Format->RecordCount
                    ); 
                    --Insert
                ) {
                    Shared->Tasks[Insert] = Shared->Tasks[Insert - 1];
                }

                Shared->Tasks[Insert] = Format;
            }
        }
    }

    LaneSync();

    if (!LaneIndex()) {
        STATIC_ASSERT(
            sizeof(Result->FileInfo.SourceMD5) == sizeof(Shared->SourceMD5),
            MD5SizeCheck
        );

        MemCpy(
            &Result->FileInfo.SourceMD5,
            &Shared->SourceMD5,
            sizeof(Result->FileInfo.SourceMD5)
        );
    }

    LaneSync();

    PROFILE(B2FGatherTimer) {
        for (;;) {
            u64 TaskIndex = AtomicIncFetchU64(
                &Shared->TaskTakeCounter, 
                MEM_ORDER_SEQ_CST
            ) - 1;

            if (TaskIndex >= Shared->TasksCount)
                break;

            B2FFormat* Format = Shared->Tasks[TaskIndex];
            FLIBakeMessageType* MessageType = Format->MessageType;
            u32 FieldCount = Format->FieldCount;
            FLIBakeColumn* Columns = MessageType->HeadColumn;
            u8* ColumnData[BIN_MAX_FIELD_COUNT] = {};
            FLIScalarKind Kinds[BIN_MAX_FIELD_COUNT] = {};
            u8 Sizes[BIN_MAX_FIELD_COUNT] = {};
            u8 Offsets[BIN_MAX_FIELD_COUNT] = {};
            b32 IsChar[BIN_MAX_FIELD_COUNT] = {};
            f64 Scales[BIN_MAX_FIELD_COUNT] = {};
            f64 Mins[BIN_MAX_FIELD_COUNT] = {};
            f64 Maxs[BIN_MAX_FIELD_COUNT] = {};
            f64 Sums[BIN_MAX_FIELD_COUNT] = {};
            u64 StartTimeUS = Shared->StartTimeUS;
            u32 TimeField = Format->TimeField;

            for (u32 Index = 0; Index < FieldCount; ++Index) {
                FLIBakeField* Field = Columns[Index].Field;

                ColumnData[Index] = (u8*) Columns[Index].Data;
                Kinds[Index] = Field->LoggedKind;
                Sizes[Index] = Field->LoggedSize;
                Offsets[Index] = Format->FieldOffsets[Index];
                Scales[Index] = Field->Multiplier;
                IsChar[Index] = B2FScalarKindIsChar(Field->LoggedKind);
                Mins[Index] = F64_MAX;
                Maxs[Index] = -F64_MAX;
                Sums[Index] = 0.0;
            }

            // NOTE(nc): the state of the columns that are made
            TempArena DerivedScratch = GetScratch(&MemPool, 1);
            u32 DerivedCount = Format->DerivedCount;
            u32 InstanceField = MessageType->InstanceField
                ? MessageType->InstanceField - 1
                : BIN_MAX_FIELD_COUNT;
            u8* DerivedData[B2F_DERIVED_MAX] = {};
            f64 DerivedMins[B2F_DERIVED_MAX] = {};
            f64 DerivedMaxs[B2F_DERIVED_MAX] = {};
            f64 DerivedSums[B2F_DERIVED_MAX] = {};
            B2FPositionState* Positions = NULL;
            B2FParamSlot* ParamSlots = NULL;
            u64 ParamSlotsCount = 0;

            for (u32 Index = 0; Index < DerivedCount; ++Index) {
                DerivedData[Index] = (u8*) Columns[FieldCount + Index].Data;
                DerivedMins[Index] = F64_MAX;
                DerivedMaxs[Index] = -F64_MAX;
            }

            if (DerivedCount && Format->HasPosition) {
                Positions = ArenaPushArrayZero(
                    DerivedScratch.MemPool,
                    B2FPositionState,
                    BIN_INSTANCE_COUNT
                );
            }

            if (DerivedCount && Format->Special == B2F_SPECIAL_KIND_PARM) {
                ParamSlotsCount = Round64NextPow2(
                    2 * Format->RecordCount + 16
                );
                ParamSlots = ArenaPushArrayZero(
                    DerivedScratch.MemPool,
                    B2FParamSlot,
                    ParamSlotsCount
                );
            }

            u64 RecordIndex = 0;

            for (
                B2FOffsetChunk* Chunk = Format->HeadOffsets; 
                Chunk; 
                Chunk = Chunk->Next
            ) {
                for (
                    u64 ChunkIndex = 0; 
                    ChunkIndex < Chunk->Count; 
                    ++ChunkIndex, ++RecordIndex
                ) {
                    u8* Body = FileData + Chunk->V[ChunkIndex] + BIN_HEAD_SIZE;

                    for (u32 Index = 0; Index < FieldCount; ++Index) {
                        u8* Src = Body + Offsets[Index];
                        u8* Dst = ColumnData[Index] + RecordIndex * Sizes[Index];
                        f64 Value = 0.0;

                        if (IsChar[Index]) {
                            MemCpy(Dst, Src, Sizes[Index]);
                            continue;
                        }

                        if (Index == TimeField) {
                            u64 TimeUS = 0;

                            MemCpy(&TimeUS, Src, sizeof(TimeUS));
                            TimeUS -= StartTimeUS;
                            MemCpy(Dst, &TimeUS, sizeof(TimeUS));
                            Value = (f64) TimeUS;
                        } else {
                            MemCpy(Dst, Src, Sizes[Index]);
                            Value = B2FF64FromScalar(
                                Src, 
                                Kinds[Index]
                            ) * Scales[Index];
                        }

                        Mins[Index] = MIN(Mins[Index], Value);
                        Maxs[Index] = MAX(Maxs[Index], Value);
                        Sums[Index] += Value;
                    }

                    if (DerivedCount) {
                        B2FPositionState* Position = NULL;
                        f64 Prev = 0.0;

                        if (Positions) {
                            u64 Instance = 0;

                            if (InstanceField < FieldCount) {
                                Instance = B2FU64FromField(
                                    Body,
                                    Format,
                                    InstanceField
                                ) % BIN_INSTANCE_COUNT;
                            }

                            Position = &Positions[Instance];
                            B2FPositionStep(
                                Shared,
                                Position,
                                B2FF64FromField(Body, Format, Format->LatField),
                                B2FF64FromField(Body, Format, Format->LngField)
                            );
                        }

                        if (ParamSlots) {
                            Prev = (f64) B2FParamPrev(
                                ParamSlots,
                                ParamSlotsCount,
                                B2FStrFromField(Body, Format, Format->NameField),
                                (f32) B2FF64FromField(
                                    Body,
                                    Format,
                                    Format->ValueField
                                )
                            );
                        }

                        for (u32 Index = 0; Index < DerivedCount; ++Index) {
                            FLIScalarKind Kind = Columns[FieldCount + Index].Kind;
                            f64 Value = 0.0;

                            switch (Format->DerivedKinds[Index]) {
                                default: {} break;

                                case B2F_DERIVED_KIND_NORTH: {
                                    Value = Position->North;
                                } break;

                                case B2F_DERIVED_KIND_EAST: {
                                    Value = Position->East;
                                } break;

                                case B2F_DERIVED_KIND_DIST: {
                                    Value = Position->Dist;
                                } break;

                                case B2F_DERIVED_KIND_PREV: {
                                    Value = Prev;
                                } break;
                            }

                            B2FScalarFromF64(
                                DerivedData[Index] + RecordIndex * FLI_SCALAR_KIND_SIZE_TABLE[Kind],
                                Kind,
                                Value
                            );
                            DerivedMins[Index] = MIN(DerivedMins[Index], Value);
                            DerivedMaxs[Index] = MAX(DerivedMaxs[Index], Value);
                            DerivedSums[Index] += Value;
                        }
                    }
                }
            }

            for (u32 Index = 0; Index < FieldCount; ++Index) {
                FLIBakeColumn* Column = &Columns[Index];

                if (IsChar[Index] || !RecordIndex) {
                    Column->Min = 0.0;
                    Column->Max = 0.0;
                    Column->Mean = 0.0;
                    continue;
                }

                Column->Min = Mins[Index];
                Column->Max = Maxs[Index];
                Column->Mean = Sums[Index] / (f64) RecordIndex;

                if (Index != TimeField && Mins[Index] == Maxs[Index]) {
                    Column->Encoding = FLI_COLUMN_ENCODING_CONSTANT;
                    Column->DataSize = 0;
                }
            }

            for (u32 Index = 0; Index < DerivedCount; ++Index) {
                FLIBakeColumn* Column = &Columns[FieldCount + Index];

                if (!RecordIndex) {
                    Column->Min = 0.0;
                    Column->Max = 0.0;
                    Column->Mean = 0.0;
                    continue;
                }

                Column->Min = DerivedMins[Index];
                Column->Max = DerivedMaxs[Index];
                Column->Mean = DerivedSums[Index] / (f64) RecordIndex;

                if (DerivedMins[Index] == DerivedMaxs[Index]) {
                    Column->Encoding = FLI_COLUMN_ENCODING_CONSTANT;
                    Column->DataSize = 0;
                }
            }

            if (TimeField < FieldCount && RecordIndex >= 2) {
                u64* Time = (u64*) ColumnData[TimeField];
                u64 BlockCount = FLITimeBlockCount(RecordIndex);
                u64 MaxDelta = 0;

                for (u64 Index = 0; Index < RecordIndex; ++Index) {
                    u64 Base = Time[Index & ~(FLI_TIME_BLOCK_SAMPLES - 1)];

                    MaxDelta = MAX(MaxDelta, Time[Index] - Base);
                }

                if (MaxDelta <= U32_MAX) {
                    TempArena TaskScratch = GetScratch(&MemPool, 1);
                    u64* Bases = ArenaPushArray(
                        TaskScratch.MemPool, 
                        u64, 
                        BlockCount
                    );
                    u32* Deltas = ArenaPushArray(
                        TaskScratch.MemPool, 
                        u32, 
                        RecordIndex
                    );

                    for (u64 Index = 0; Index < RecordIndex; ++Index) {
                        u64 Block = Index >> FLI_TIME_BLOCK_SHIFT;

                        if (!(Index & (FLI_TIME_BLOCK_SAMPLES - 1)))
                            Bases[Block] = Time[Index];

                        Deltas[Index] = (u32) (Time[Index] - Bases[Block]);
                    }

                    MemCpy(Time, Bases, sizeof(u64) * BlockCount);
                    MemCpy(Time + BlockCount, Deltas, sizeof(u32) * RecordIndex);
                    Columns[TimeField].Encoding = FLI_COLUMN_ENCODING_DELTA_TIME;
                    Columns[TimeField].DataSize = (
                        sizeof(u64) * BlockCount + 
                        sizeof(u32) * RecordIndex
                    );
                    ReleaseScratch(TaskScratch);
                }
            }

            ReleaseScratch(DerivedScratch);
        }
    }

    LaneSync();

    FLIBakeParams Copy = *Result;

    LaneSync();
    ReleaseScratch(Scratch);

    return Copy;
}
