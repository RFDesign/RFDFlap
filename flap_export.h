#if !defined(__FLAP_EXPORT_H__)
#define __FLAP_EXPORT_H__

#include "nc_types.h"
#include "nc_math.h"
#include "nc_string.h"
#include "nc_system.h"

#include "flap_widgets.h"

struct Arena;
struct FLAPTableParams;
struct FLAPExportSource;
struct FLAPExportWriter;

// @defines____________________________________________________________________
#define FLAP_EXPORT_CHUNK_SIZE MB(1)
#define FLAP_EXPORT_ROW_SIZE     64
#define FLAP_EXPORT_F64_DECIMALS 7
#define FLAP_EXPORT_F64_SCALE    10000000ULL

typedef u32 FLAPExportKind;
enum : u32 {
    FLAP_EXPORT_KIND_NULL,
    FLAP_EXPORT_KIND_PNG,
    FLAP_EXPORT_KIND_CSV,
    FLAP_EXPORT_KIND_TXT,
    FLAP_EXPORT_KIND_COUNT
};

#define FLAP_EXPORT_FUNC_DEF(X)  b32 X(Arena* MemPool, FLAPExportSource const* Source, FLAPExportWriter* Writer)
#define FLAP_EXPORT_FUNC_NAME(X) __FlapExportFunc##X
#define FLAP_EXPORT_FUNC_SIG(X)  FLAP_EXPORT_FUNC_DEF(FLAP_EXPORT_FUNC_NAME(X))
typedef FLAP_EXPORT_FUNC_DEF(FLAPExportFuncType);

// @types______________________________________________________________________
struct FLAPExportSource {
    u8*              Pixels;
    FLAPTableParams* Table;
    v2i32            Size;
};

struct FLAPExportWriter {
    Handle File;
    Str8   FilePath;
    Str8   TempPath;
    u8*    Chunk;
    u64    Size;
    u64    Capacity;
    u64    Offset;
    b32    HasFailed;
    u32    __Padding;
};

typedef FLAP_EXPORT_FUNC_SIG(FLAPExportFunc);

// @runtime____________________________________________________________________
extern Str8 FLAP_EXPORT_KIND_EXTENSION_TABLE[FLAP_EXPORT_KIND_COUNT];
extern FLAPExportFuncType* FLAP_EXPORT_KIND_FUNC_TABLE[FLAP_EXPORT_KIND_COUNT];

// @functions__________________________________________________________________
FLAPExportWriter FLAPExportOpen(Arena* MemPool, Str8 FilePath, u8* Chunk);
b32 FLAPExportClose(FLAPExportWriter* Writer, b32 IsOkay);
void FLAPExportFlush(FLAPExportWriter* Writer);
void FLAPExportWrite(FLAPExportWriter* Writer, Str8 String);
u8* FLAPExportReserve(FLAPExportWriter* Writer, u64 Size);
void FLAPExportCommit(FLAPExportWriter* Writer, u8* Ptr);
u8* FLAPExportPutU64(u8* Ptr, u64 Value);
u8* FLAPExportPutPadded(u8* Ptr, u64 Value, u32 Width);
u8* FLAPExportPutI64(u8* Ptr, i64 Value);
u8* FLAPExportPutTime(u8* Ptr, u64 TimeUSecs);
u8* FLAPExportPutF64(u8* Ptr, f64 Value);
Str8 FLAPStrFromCSVField(Arena* MemPool, Str8 Field);
void FLAPExportTableRow(Arena* MemPool, FLAPTableCtx* Ctx, u64 Row, OUT Str8* Cells, OUT u64* TimeUSecs);
b32 FLAPExport(FLAPExportKind Kind, FLAPExportSource const* Source, Str8 FilePath);

FLAP_EXPORT_FUNC_SIG(PNG);
FLAP_EXPORT_FUNC_SIG(CSV);
FLAP_EXPORT_FUNC_SIG(TXT);

#endif // __FLAP_EXPORT_H__
