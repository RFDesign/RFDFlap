#if !defined(__FLI_BIN_H__)
#define __FLI_BIN_H__

#include "nc_types.h"
#include "nc_string.h"
#include "fli.h"

// @defines____________________________________________________________________
#define BIN_HEAD_BYTE_0      0xA3
#define BIN_HEAD_BYTE_1      0x95
#define BIN_HEAD_SIZE        3
#define BIN_FMT_TYPE         0x80
#define BIN_FMT_LENGTH       89
#define BIN_TYPE_COUNT       256
#define BIN_MAX_FIELD_COUNT  16

#define BIN_GPS_EPOCH_UNIX_SECS 315964800ULL
#define BIN_GPS_WEEK_SECS       604800ULL
#define BIN_GPS_LEAP_SECS       18ULL

#define BIN_CMD_ID_NAV_LAND      21
#define BIN_INSTANCE_COUNT       256
#define BIN_EARTH_RADIUS_METRES  6378100.0
#define BIN_DERIVED_FORMAT_CHAR  '~'
#define B2F_DERIVED_MAX          4

typedef u32 B2FSpecialKind;
enum : u32 {
    B2F_SPECIAL_KIND_NONE,
    B2F_SPECIAL_KIND_FMT,
    B2F_SPECIAL_KIND_FMTU,
    B2F_SPECIAL_KIND_UNIT,
    B2F_SPECIAL_KIND_MULT,
    B2F_SPECIAL_KIND_PARM,
    B2F_SPECIAL_KIND_MSG,
    B2F_SPECIAL_KIND_MODE,
    B2F_SPECIAL_KIND_EV,
    B2F_SPECIAL_KIND_ERR,
    B2F_SPECIAL_KIND_COUNT
};

typedef u8 B2FDerivedKind;
enum : u8 {
    B2F_DERIVED_KIND_NULL,
    B2F_DERIVED_KIND_NORTH,
    B2F_DERIVED_KIND_EAST,
    B2F_DERIVED_KIND_DIST,
    B2F_DERIVED_KIND_PREV,
    B2F_DERIVED_KIND_COUNT
};

// @types______________________________________________________________________
struct B2FConvertParams {
    Str8 InputBINName;
    Str8 InputBINData;
};

struct B2FFormatCharInfo {
    FLIScalarKind Kind;
    u8            Size;
    f64           Scale;
};

struct B2FDerivedInfo {
    Str8          Name;
    Str8          Unit;
    FLIScalarKind Kind;
};

struct B2FPositionState {
    f64 Lat;
    f64 Lng;
    f64 North;
    f64 East;
    f64 Dist;
    b32 HasPosition;
    u32 __Padding;
};

struct B2FParamSlot {
    u8  Name[16];
    f32 Value;
    b32 IsUsed;
};

struct B2FOffsetChunk {
    B2FOffsetChunk* Next;
    u64*            V;
    u64             Count;
    u64             Capacity;
};

struct B2FFormat {
    u8                  Type;
    u8                  Length;
    B2FSpecialKind      Special;
    b32                 Supported;
    b32                 HasTime;
    Str8                Name;
    Str8                FormatString;
    u32                 FieldCount;
    u32                 TimeField;
    u32                 InstanceField;
    u32                 WeekField;
    u32                 MSecsField;
    u32                 StatusField;
    b32                 HasClock;
    b32                 IsGPS;
    b32                 HasPosition;
    u32                 LatField;
    u32                 LngField;
    u32                 CommandField;
    u32                 NameField;
    u32                 ValueField;
    u32                 DerivedCount;
    B2FDerivedKind      DerivedKinds[B2F_DERIVED_MAX];
    Str8                FieldNames[BIN_MAX_FIELD_COUNT];
    u8                  FieldChars[BIN_MAX_FIELD_COUNT];
    u8                  FieldOffsets[BIN_MAX_FIELD_COUNT];
    u8                  UnitIds[BIN_MAX_FIELD_COUNT];
    u8                  MultIds[BIN_MAX_FIELD_COUNT];
    u64                 RecordCount;
    B2FOffsetChunk*     HeadOffsets;
    B2FOffsetChunk*     TailOffsets;
    FLIBakeMessageType* MessageType;
};

// @functions__________________________________________________________________
FLIBakeParams B2FConvert(Arena* MemPool, B2FConvertParams* Params);
void B2FMD5ThreadEntryPoint(void* __Params);

#endif // __FLI_BIN_H__
