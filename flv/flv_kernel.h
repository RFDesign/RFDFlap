#if !defined(__FLV_KERNEL_H__)
#define __FLV_KERNEL_H__

#include "nc_types.h"
#include "nc_string.h"

struct FLVLane;
struct FLVRegion;

// @defines____________________________________________________________________
typedef u8 FLVElementKind;
enum : u8 {
    FLV_ELEMENT_KIND_NONE,
    FLV_ELEMENT_KIND_I64,
    FLV_ELEMENT_KIND_F64,
    FLV_ELEMENT_KIND_MASK,
    FLV_ELEMENT_KIND_SELECTION,
    FLV_ELEMENT_KIND_SINGLE_I64,
    FLV_ELEMENT_KIND_SINGLE_F64,
    FLV_ELEMENT_KIND_COUNT
};

typedef u16 FLVOperand;
enum : u16 {
    FLV_OPERAND_KIND_TEMP,
    FLV_OPERAND_KIND_SLOT,
    FLV_OPERAND_KIND_CONST,
    FLV_OPERAND_KIND_NONE
};

#define FLV_OPERAND_KIND_SHIFT 14
#define FLV_OPERAND_INDEX_MASK 0x3FFFU
#define FLV_OPERAND_NONE       0xFFFFU

#define FLV_WRITE_CONVERSION_INVALID 0xFF

#define FLV_KERNEL_FLAG_RUN       (1 << 0)
#define FLV_KERNEL_FLAG_SCALAR    (1 << 1)
#define FLV_KERNEL_FLAG_REDUCTION (1 << 2)
#define FLV_KERNEL_FLAG_WINDOW    (1 << 3)
#define FLV_KERNEL_FLAG_EMIT      (1 << 4)
#define FLV_KERNEL_FLAG_ROLLING   (1 << 5)

#define FLV_SELECTION_CHUNK_RANGES U8_MAX
#define FLV_VALUE_CHUNK_BYTES      KB(16)

#define FLV_ARITH_HEAD(Type)                                                \
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) { \
        FLVTempSetMissing(Lane, Op.Dst);                                    \
                                                                            \
        return;                                                             \
    }                                                                       \
                                                                            \
    Type* Dst = (Type*) FLVTempPtr(Lane, Op.Dst);                           \
    u64   Count = Lane->ChunkCount

#define FLV_OP_ADD(A, B)     ((A) + (B))
#define FLV_OP_SUB(A, B)     ((A) - (B))
#define FLV_OP_MUL(A, B)     ((A) * (B))
#define FLV_OP_DIV_I64(A, B) ((B) ? (A) / (B) : FLV_I64_HOLE)
#define FLV_OP_DIV_F64(A, B) ((A) / (B))
#define FLV_OP_MOD_I64(A, B) ((B) ? (A) % (B) : FLV_I64_HOLE)
#define FLV_OP_MOD_F64(A, B) FMod((A), (B))

#define FLV_EVENT_ID_ARMED    10
#define FLV_EVENT_ID_DISARMED 11

#define FLV_KERNEL_LIST(X)                                                                                        \
    X(NONE,                0, NONE,       0)                                                                      \
    X(LOAD_I64,            1, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(LOAD_F64,            1, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(LOAD_INSTANCE,       0, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(LOAD_HOLD_I64,       0, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(LOAD_HOLD_F64,       0, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(CONVERT_I64_F64,     1, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(ADD_I64_T,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(ADD_I64_S,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(ADD_F64_T,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(ADD_F64_S,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(SUB_I64_T,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(SUB_I64_S,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(SUB_I64_RS,          2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(SUB_F64_T,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(SUB_F64_S,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(SUB_F64_RS,          2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MUL_I64_T,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MUL_I64_S,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MUL_F64_T,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MUL_F64_S,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(DIV_I64_T,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(DIV_I64_S,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(DIV_I64_RS,          2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(DIV_F64_T,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(DIV_F64_S,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(DIV_F64_RS,          2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MOD_I64_T,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MOD_I64_S,           2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MOD_I64_RS,          2, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MOD_F64_T,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MOD_F64_S,           2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(MOD_F64_RS,          2, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(NEG_I64,             1, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(NEG_F64,             1, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(ABS_I64,             1, I64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(ABS_F64,             1, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(SQRT_F64,            1, F64,        FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_LT_I64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_LT_I64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_LT_F64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_LT_F64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_LE_I64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_LE_I64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_LE_F64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_LE_F64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_GT_I64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_GT_I64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_GT_F64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_GT_F64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_GE_I64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_GE_I64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_GE_F64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_GE_F64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_EQ_I64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_EQ_I64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_EQ_F64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_EQ_F64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_NE_I64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_NE_I64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_NE_F64_T,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(CMP_NE_F64_S,        2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(MASK_AND,            2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(MASK_OR,             2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(MASK_NOT,            1, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(STR_EQ,              2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(STR_NE,              2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(STR_HAS,             2, MASK,       FLV_KERNEL_FLAG_RUN)                                                    \
    X(MEAN_I64,            1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(MEAN_F64,            1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(STDDEV_I64,          1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(STDDEV_F64,          1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(MIN_I64,             1, SINGLE_I64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(MIN_F64,             1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(MAX_I64,             1, SINGLE_I64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(MAX_F64,             1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(SUM_I64,             1, SINGLE_I64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(SUM_F64,             1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(FIRST_I64,           1, SINGLE_I64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(FIRST_F64,           1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(LAST_I64,            1, SINGLE_I64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(LAST_F64,            1, SINGLE_F64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(REDUCE_COUNT,        1, SINGLE_I64, FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(DIFF_I64,            1, I64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW)                           \
    X(DIFF_F64,            1, F64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW)                           \
    X(RATE_I64,            1, F64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW)                           \
    X(RATE_F64,            1, F64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW)                           \
    X(ROLLING_MEAN_I64,    2, I64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW | FLV_KERNEL_FLAG_ROLLING) \
    X(ROLLING_MEAN_F64,    2, F64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW | FLV_KERNEL_FLAG_ROLLING) \
    X(ROLLING_MIN_I64,     2, I64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW | FLV_KERNEL_FLAG_ROLLING) \
    X(ROLLING_MIN_F64,     2, F64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW | FLV_KERNEL_FLAG_ROLLING) \
    X(ROLLING_MAX_I64,     2, I64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW | FLV_KERNEL_FLAG_ROLLING) \
    X(ROLLING_MAX_F64,     2, F64,        FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_WINDOW | FLV_KERNEL_FLAG_ROLLING) \
    X(SEL_FROM_MASK,       1, SELECTION,  FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_REDUCTION)                        \
    X(SEL_FROM_TIME_RANGE, 2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_MODE,            1, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_EVENT,           1, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_ARMED,           0, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_AND,             2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_OR,              2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_NOT,             1, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_CLOSE,           2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_LONGER,          2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_TOUCH,           2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_BEFORE,          2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_AFTER,           2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_PICK,            2, SELECTION,  FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_DURATION,        1, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_COUNT,           1, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_START,           1, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(SEL_END,             1, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_ADD_I64,           2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_ADD_F64,           2, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_SUB_I64,           2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_SUB_F64,           2, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_MUL_I64,           2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_MUL_F64,           2, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_DIV_I64,           2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_DIV_F64,           2, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_MOD_I64,           2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_MOD_F64,           2, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_NEG_I64,           1, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_NEG_F64,           1, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_ABS_I64,           1, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_ABS_F64,           1, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_SQRT_F64,          1, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_LT_I64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_LT_F64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_LE_I64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_LE_F64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_GT_I64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_GT_F64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_GE_I64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_GE_F64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_EQ_I64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_EQ_F64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_NE_I64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CMP_NE_F64,        2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_AND,               2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_OR,                2, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_NOT,               1, SINGLE_I64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(S_CONVERT_I64_F64,   1, SINGLE_F64, FLV_KERNEL_FLAG_SCALAR)                                                 \
    X(EMIT_CURVE,          1, NONE,       FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_EMIT)                             \
    X(EMIT_TABLE_COLUMN,   1, NONE,       FLV_KERNEL_FLAG_RUN | FLV_KERNEL_FLAG_EMIT)                             \
    X(EMIT_FLAG,           1, NONE,       FLV_KERNEL_FLAG_SCALAR | FLV_KERNEL_FLAG_EMIT)                          \
    X(EMIT_FLAG_SCALAR,    1, NONE,       FLV_KERNEL_FLAG_SCALAR | FLV_KERNEL_FLAG_EMIT)                          \
    X(EMIT_MISSING,        0, NONE,       FLV_KERNEL_FLAG_SCALAR | FLV_KERNEL_FLAG_EMIT)                          \
    X(EMIT_WRITE_I64,      1, NONE,       FLV_KERNEL_FLAG_SCALAR | FLV_KERNEL_FLAG_EMIT)                          \
    X(EMIT_WRITE_F64,      1, NONE,       FLV_KERNEL_FLAG_SCALAR | FLV_KERNEL_FLAG_EMIT)                          \
    X(EMIT_WRITE_NONE,     0, NONE,       FLV_KERNEL_FLAG_SCALAR | FLV_KERNEL_FLAG_EMIT)                          \
    X(EMIT_WRITE,          0, NONE,       FLV_KERNEL_FLAG_SCALAR | FLV_KERNEL_FLAG_EMIT)                          \
    X(EMIT_WRITE_ROWS,     1, NONE,       FLV_KERNEL_FLAG_SCALAR | FLV_KERNEL_FLAG_EMIT)                          \

#define X(Name, Inputs, Dst, Flags) FLV_KERNEL_##Name,

typedef u16 FLVKernelID;
enum : u16 {
    FLV_KERNEL_LIST(X)
    FLV_KERNEL_COUNT
};

#undef X

typedef u8 FLVTouchKind;
enum : u8 {
    FLV_TOUCH_KIND_ANY,
    FLV_TOUCH_KIND_BEFORE,
    FLV_TOUCH_KIND_AFTER
};

// @types______________________________________________________________________
struct FLVKernelInfo {
    u8 InputCount;
    u8 DstKind;
    u8 Flags;
    u8 __Padding;
};

struct FLVOp {
    u16 Kernel;
    FLVOperand Dst;
    FLVOperand LHS;
    FLVOperand RHS;
};
STATIC_ASSERT(sizeof(FLVOp) == 8, FLVOpSizeCheck);

union FLVNumber {
    f64 F64;
    i64 I64;
};

struct FLVReduction {
    FLVNumber Summation;
    FLVNumber Minimum;
    FLVNumber Maximum;
    FLVNumber Head;
    FLVNumber Tail;
    f64       SummationSquared;
    f64       Shift;
    u64       Count;
};

struct FLVWindow {
    u64*       Times;
    FLVNumber* Values;
    u64        Capacity;
    u64        Head;
    u64        Count;
    FLVNumber  Summation;
    FLVNumber  Prev;
    u64        PrevTimeUSecs;
    b32        HasPrev;
    u32        __Padding;
};

struct FLVHold {
    u64 Cursor;
    u64 Held;
    b32 HasHeld;
    u32 __Padding;
};

struct FLVSelectionChunk {
    FLVSelectionChunk* Next;
    u64                Count;
    r1u64              V[FLV_SELECTION_CHUNK_RANGES];
};

struct FLVSelectionBuild {
    FLVSelectionChunk* Head;
    FLVSelectionChunk* Tail;
    u64                Count;
    u64                OpenStartUSecs;
    u64                OpenEndUSecs;
    b32                IsOpen;
    u32                __Padding;
};

struct FLVValueChunk {
    FLVValueChunk* Next;
    u64            Count;
    u8             V[FLV_VALUE_CHUNK_BYTES];
};

struct FLVEmitBuild {
    FLVValueChunk* Head;
    FLVValueChunk* Tail;
    u64            ByteCount;
    u32            SourceSlot;
    u32            Width;
};

struct FLVWriteSpec {
    Str8 Literal;
    i32  Width;
    i32  Precision;
    b32  HaveMinus;
    b32  HavePlus;
    b32  HaveSpace;
    b32  HaveHash;
    b32  HaveZero;
    u8   Conversion;
    u8   __Padding[3];
};

struct FLVWriteValue {
    FLVNumber V;
    u8        Kind;
    u8        IsMissing;
    u8        __Padding[6];
};

// @defines____________________________________________________________________
#define FLV_KERNEL_FUNC_SIG(X) void X(FLVLane* Lane, FLVOp Op)
typedef FLV_KERNEL_FUNC_SIG(FLVKernelFuncType);

// @runtime____________________________________________________________________
extern FLVKernelInfo const FLV_KERNEL_INFO_TABLE[FLV_KERNEL_COUNT];
extern Str8 const FLV_KERNEL_NAME_TABLE[FLV_KERNEL_COUNT];
extern u64 const FLV_ELEMENT_KIND_SIZE_TABLE[FLV_ELEMENT_KIND_COUNT];
extern FLVKernelFuncType* const FLV_KERNEL_TABLE[FLV_KERNEL_COUNT];

// @functions__________________________________________________________________
void FLVKernelsRegionBegin(FLVLane* Lane, FLVRegion const* Region);
void FLVKernelsRegionEnd(FLVLane* Lane, FLVRegion const* Region);
void FLVKernelsRangeReset(FLVLane* Lane, FLVRegion const* Region);
f64 FLVScalarF64(FLVLane* Lane, FLVOperand Operand);
i64 FLVScalarI64(FLVLane* Lane, FLVOperand Operand);

INTERNAL FLVOperand
FLVOperandAlloc(u16 Kind, u32 Index)
{
    ASSERT(Index <= FLV_OPERAND_INDEX_MASK);

    FLVOperand Result = (FLVOperand) (
        (Kind << FLV_OPERAND_KIND_SHIFT) | Index
    );

    return Result;
}

INTERNAL u16
FLVGetOperandKind(FLVOperand Operand)
{
    return Operand >> FLV_OPERAND_KIND_SHIFT;
}

INTERNAL u32
FLVGetOperandIndex(FLVOperand Operand)
{
    return Operand & FLV_OPERAND_INDEX_MASK;
}

// NOTE(nc): the next part of a write format, read as __VSNPrintf reads a
//         : format: the text up to a %, the flags, the width, the
//         : precision and the conversion. Conversion is 0 for text alone,
//         : as at the end of the format and for %%, which is one % of the
//         : text. a * is not legal, since a write has no argument for it.
INTERNAL b32
FLVWriteSpecNext(Str8 Format, u64* Offset, OUT FLVWriteSpec* Spec)
{
    u8 const* FmtPtr = Format.Str + *Offset;
    u8 const* FmtEnd = Format.Str + Format.Size;
    u8 const* NextSpecifier = FmtPtr;

    *Spec = {};
    Spec->Precision = -1;

    if (FmtPtr >= FmtEnd)
        return FALSE;

    while (NextSpecifier < FmtEnd && *NextSpecifier != '%')
        ++NextSpecifier;

    Spec->Literal = Str((u8*) FmtPtr, (u64) (NextSpecifier - FmtPtr));
    FmtPtr = NextSpecifier;

    if (FmtPtr < FmtEnd) {
        ++FmtPtr;

        if (FmtPtr < FmtEnd && *FmtPtr == '%') {
            ++Spec->Literal.Size;
            ++FmtPtr;
        } else {
            while (FmtPtr < FmtEnd) {
                const u8 CurrFmt = *FmtPtr;

                if (CurrFmt == '-')
                    Spec->HaveMinus = TRUE;
                else if (CurrFmt == '+')
                    Spec->HavePlus = TRUE;
                else if (CurrFmt == ' ')
                    Spec->HaveSpace = TRUE;
                else if (CurrFmt == '#')
                    Spec->HaveHash = TRUE;
                else if (CurrFmt == '0')
                    Spec->HaveZero = TRUE;
                else
                    break;

                ++FmtPtr;
            }

            while (FmtPtr < FmtEnd && IsDigit(*FmtPtr)) {
                Spec->Width = Spec->Width * 10 + (*FmtPtr - '0');
                ++FmtPtr;
            }

            if (FmtPtr < FmtEnd && *FmtPtr == '.') {
                ++FmtPtr;
                Spec->Precision = 0;

                while (FmtPtr < FmtEnd && IsDigit(*FmtPtr)) {
                    Spec->Precision = Spec->Precision * 10 + (*FmtPtr - '0');
                    ++FmtPtr;
                }
            }

            // NOTE(nc): a value of a write is 64 bits, so a length says
            //         : nothing. it is read so that %lld is legal.
while (
                FmtPtr < FmtEnd &&
                (
                    *FmtPtr == 'h' ||
                    *FmtPtr == 'l' ||
                    *FmtPtr == 'z'
                )
            ) {
                ++FmtPtr;
            }

            Spec->Conversion = FLV_WRITE_CONVERSION_INVALID;

            if (FmtPtr < FmtEnd) {
                u8 C = *FmtPtr++;

                if (
                    C == 'd' || C == 'i' || C == 'f' ||
                    C == 'F' || C == 'm' || C == 't'
                ) {
                    Spec->Conversion = C;
                } else {
                    Spec->Width = (i32) C;
                }
            }
        }
    }

    *Offset = (u64) (FmtPtr - Format.Str);

    return TRUE;
}

#endif // __FLV_KERNEL_H__
