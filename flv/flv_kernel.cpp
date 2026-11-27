#include "nc_defines.h"
#include "nc_types.h"
#include "nc_math.h"
#include "nc_memory.h"
#include "nc_arena.h"
#include "nc_string.h"
#include "nc_time.h"
#include "nc_log.h"
#include "nc_simd.h"

#include "fli.h"

#include "flv_kernel.h"
#include "flv_lower.h"
#include "flv_bind.h"
#include "flv_run.h"
#include "flv_stdlib.h"

#define X(Name, Inputs, Dst, Flags) { Inputs, FLV_ELEMENT_KIND_##Dst, Flags, 0 },
FLVKernelInfo const FLV_KERNEL_INFO_TABLE[FLV_KERNEL_COUNT] = {
    FLV_KERNEL_LIST(X)
};
#undef X

#define X(Name, Inputs, Dst, Flags) Str8Lit(#Name),
Str8 const FLV_KERNEL_NAME_TABLE[FLV_KERNEL_COUNT] = {
    FLV_KERNEL_LIST(X)
};
#undef X

u64 const FLV_ELEMENT_KIND_SIZE_TABLE[FLV_ELEMENT_KIND_COUNT] = {
    0,
    FLV_CHUNK_SAMPLES * sizeof(i64),
    FLV_CHUNK_SAMPLES * sizeof(i64),
    FLV_CHUNK_MASK_WORDS * sizeof(u64),
    sizeof(FLVSelection),
    sizeof(i64),
    sizeof(f64)
};

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelStub)
{
    LogError(
        "kernel %S is not implemented yet",
        PRINT_STR(FLV_KERNEL_NAME_TABLE[Op.Kernel])
    );
    FLVTempSetMissing(Lane, Op.Dst);
}

f64
FLVScalarF64(FLVLane* Lane, FLVOperand Operand)
{
    f64 Result = 0.0;

    switch (FLVGetOperandKind(Operand)) {
        default: {
            ASSERT(FALSE);
        } break;

        case FLV_OPERAND_KIND_CONST: {
            FLVConst const* Const = &Lane->Program->Consts[
                FLVGetOperandIndex(Operand)
            ];

            Result = (Const->Kind == FLV_CONST_KIND_F64)
                ? Const->F64
                : (f64) Const->I64;
        } break;

        case FLV_OPERAND_KIND_TEMP: {
            Result = *(f64 const*) FLVTempPtr(Lane, Operand);
        } break;
    }

    return Result;
}

i64
FLVScalarI64(FLVLane* Lane, FLVOperand Operand)
{
    i64 Result = 0;

    switch (FLVGetOperandKind(Operand)) {
        default: {
            ASSERT(FALSE);
        } break;

        case FLV_OPERAND_KIND_CONST: {
            FLVConst const* Const = &Lane->Program->Consts[
                FLVGetOperandIndex(Operand)
            ];

            Result = (Const->Kind == FLV_CONST_KIND_F64)
                ? (i64) Const->F64
                : Const->I64;
        } break;

        case FLV_OPERAND_KIND_TEMP: {
            Result = *(i64 const*) FLVTempPtr(Lane, Operand);
        } break;
    }

    return Result;
}

INTERNAL u64
FLVScalarTimeUSecs(FLVLane* Lane, FLVOperand Operand)
{
    u64 Result = 0;

    if (FLVGetOperandKind(Operand) == FLV_OPERAND_KIND_CONST)
        Result = Lane->Program->Consts[FLVGetOperandIndex(Operand)].TimeUSecs;
    else
        Result = *(u64 const*) FLVTempPtr(Lane, Operand);

    return Result;
}


INTERNAL FLVSelection
FLVSelectionAlloc(FLVLane* Lane, u64 Capacity)
{
    FLVSelection Result = {};

    Result.V = ArenaPushArray(Lane->MemPool, r1u64, Capacity);
    Result.Count = 0;

    return Result;
}

INTERNAL void
FLVSelectionBuildPush(FLVLane* Lane, FLVSelectionBuild* Build, r1u64 Range)
{
    FLVSelectionChunk* Chunk = Build->Tail;

    if (!Chunk || Chunk->Count == FLV_SELECTION_CHUNK_RANGES) {
        Chunk = ArenaPushStruct(Lane->MemPool, FLVSelectionChunk);
        Chunk->Next = NULL;
        Chunk->Count = 0;
        SLL_QUEUE_PUSH(Build->Head, Build->Tail, Chunk);
    }

    Chunk->V[Chunk->Count] = Range;
    ++Chunk->Count;
    ++Build->Count;
}

INTERNAL FLVSelection
FLVSelectionFromBuild(FLVLane* Lane, FLVSelectionBuild const* Build)
{
    FLVSelection Result = FLVSelectionAlloc(Lane, Build->Count);

    for (
        FLVSelectionChunk* Chunk = Build->Head; 
        Chunk; 
        Chunk = Chunk->Next
    ) {
        MemCpy(
            Result.V + Result.Count, 
            Chunk->V, 
            Chunk->Count * sizeof(r1u64)
        );
        Result.Count += Chunk->Count;
    }

    return Result;
}

INTERNAL void
FLVMaskOrBits(u64* Dst, u64 DstBit, u64 const* Src, u64 Count)
{
    for (u64 Word = 0; (Word << 6) < Count; ++Word) {
        u64 Bits = Src[Word];
        u64 Valid = Count - (Word << 6);
        u64 Position = DstBit + (Word << 6);
        u64 Shift = Position & 63;
        u64 DstWord = Position >> 6;

        if (Valid < 64)
            Bits &= (1ULL << Valid) - 1;

        Dst[DstWord] |= Bits << Shift;

        if (Shift)
            Dst[DstWord + 1] |= Bits >> (64 - Shift);
    }
}

INTERNAL void
FLVMaskSetRange(u64* Dst, u64 DstBit, u64 Count)
{
    for (u64 Index = 0; Index < Count; ) {
        u64 Position = DstBit + Index;
        u64 Shift = Position & 63;
        u64 Take = MIN(64 - Shift, Count - Index);
        u64 Bits = (Take == 64) ? U64_MAX : ((1ULL << Take) - 1);

        Dst[Position >> 6] |= Bits << Shift;
        Index += Take;
    }
}

INTERNAL void
FLVMaskSetBit(u64* Dst, u64 Position)
{
    Dst[Position >> 6] |= 1ULL << (Position & 63);
}

INTERNAL u32*
FLVRankFromMask(Arena* MemPool, u64 const* Mask, u64 SampleCount)
{
    u64 Words = FLVSeriesWordCount(SampleCount);
    u32* Result = ArenaPushArray(MemPool, u32, Words + 1);
    u64 Total = 0;

    ASSERT(SampleCount <= U32_MAX);

    for (u64 Word = 0; Word < Words; ++Word) {
        Result[Word] = (u32) Total;
        Total += PopCount64(Mask[Word]);
    }

    Result[Words] = (u32) Total;

    return Result;
}

INTERNAL void
FLVEmitBuildPush(FLVLane* Lane, FLVEmitBuild* Build, void const* Src, u64 Bytes)
{
    FLVValueChunk* Chunk = Build->Tail;

    if (!Chunk || Chunk->Count + Bytes > FLV_VALUE_CHUNK_BYTES) {
        Chunk = ArenaPushStruct(Lane->MemPool, FLVValueChunk);
        Chunk->Next = NULL;
        Chunk->Count = 0;
        SLL_QUEUE_PUSH(Build->Head, Build->Tail, Chunk);
    }

    MemCpy(Chunk->V + Chunk->Count, Src, Bytes);
    Chunk->Count += Bytes;
    Build->ByteCount += Bytes;
}

INTERNAL void*
FLVEmitBuildFlatten(FLVLane* Lane, FLVEmitBuild* Build)
{
    u8* Result = (u8*) ArenaPushSize(
        Lane->ResultMemPool, 
        Build->ByteCount, 
        8, 
        FALSE
    );
    u64 Offset = 0;

    for (FLVValueChunk* Chunk = Build->Head; Chunk; Chunk = Chunk->Next) {
        MemCpy(Result + Offset, Chunk->V, Chunk->Count);
        Offset += Chunk->Count;
    }

    return Result;
}

no_inline internal u32
FLVEmitSourceSlot(FLVLane* Lane, FLVRegion const* Region, u32 OpIndex, FLVOperand Operand)
{
    if (FLVGetOperandKind(Operand) == FLV_OPERAND_KIND_SLOT)
        return FLVGetOperandIndex(Operand);

    if (FLVGetOperandKind(Operand) != FLV_OPERAND_KIND_TEMP)
        return U32_MAX;

    for (u32 Index = OpIndex; Index--; ) {
        FLVOp P = Lane->Program->Ops[Region->HeadOp + Index];

        if (P.Dst != Operand)
            continue;

        switch (P.Kernel) {
            default: {
                return U32_MAX;
            } break;

            case FLV_KERNEL_LOAD_I64:
            case FLV_KERNEL_LOAD_F64: {
                return FLVGetOperandIndex(P.LHS);
            } break;

            case FLV_KERNEL_CONVERT_I64_F64: {
                return FLVEmitSourceSlot(Lane, Region, Index, P.LHS);
            } break;
        }
    }

    return U32_MAX;
}

INTERNAL r1u64*
FLVRegionRangesCpy(FLVLane* Lane)
{
    r1u64* Result = ArenaPushArray(
        Lane->ResultMemPool, 
        r1u64, 
        Lane->RangesCount
    );

    MemCpy(Result, Lane->Ranges, Lane->RangesCount * sizeof(r1u64));

    return Result;
}

INTERNAL u64
FLVMaskHead(u64 const* Mask, u64 Count)
{
    u64 Result = Count;
    u64 Words = (Count + 63) >> 6;

    for (u64 Word = 0; Word < Words; ++Word) {
        if (Mask[Word]) {
            Result = MIN((Word << 6) + Ctz64(Mask[Word]), Count);
            break;
        }
    }
 
    return Result;
}
 
INTERNAL u64
FLVMaskTail(u64 const* Mask, u64 Count)
{
    u64 Result = Count;
    u64 Words = (Count + 63) >> 6;
    u64 TailBits = Count & 63;
    u64 TailMask = TailBits ? (1ULL << TailBits) - 1 : U64_MAX;

    for (u64 Word = Words; Word--; ) {
        u64 V = (Word == Words - 1) ? Mask[Word] & TailMask : Mask[Word];

        if (V) {
            Result = (Word << 6) + 64 - Clz64(V);
            break;
        }
    }
 
    return Result;
}

#define FLV_LOAD_LOOP(SrcType, DstType)                              \
    do {                                                             \
        SrcType const* __Src = (SrcType const*) Column->Data + Base; \
        DstType* __Dst = (DstType*) Result;                          \
                                                                     \
        for (u64 Index = 0; Index < Count; ++Index)                  \
            __Dst[Index] = (DstType) __Src[Index];                   \
    } while (FALSE)
INTERNAL void
FLVLoadChunk(
    FLVLane* Lane,
    FLVColumn const* Column,
    FLVOperand Dst,
    b32 ToF64
) {
    void* Result = FLVTempPtr(Lane, Dst);
    u64 Base = Lane->ChunkBase;
    u64 Count = Lane->ChunkCount;

    if (Column->IsMissing) {
        FLVTempSetMissing(Lane, Dst);

        return;
    }

    if (Column->Encoding == FLI_COLUMN_ENCODING_CONSTANT) {
        if (ToF64) {
            f64* D = (f64*) Result;
            f64 V = Column->Constant;

            for (u64 Index = 0; Index < Count; ++Index)
                D[Index] = V;
        } else {
            i64* D = (i64*) Result;
            i64 V = (i64) Column->Constant;

            for (u64 Index = 0; Index < Count; ++Index)
                D[Index] = V;
        }

        return;
    }

    if (Column->Encoding == FLI_COLUMN_ENCODING_DELTA_TIME) {
        u64 const* Bases = (u64 const*) Column->Data;

        if (ToF64) {
            f64* D = (f64*) Result;

            for (u64 Index = 0; Index < Count; ++Index) {
                u64 Sample = Base + Index;

                D[Index] = (f64) (
                    Bases[Sample >> FLI_TIME_BLOCK_SHIFT] + 
                    Column->Deltas[Sample]
                );
            }
        } else {
            i64* D = (i64*) Result;

            for (u64 Index = 0; Index < Count; ++Index) {
                u64 Sample = Base + Index;

                D[Index] = (i64) (
                    Bases[Sample >> FLI_TIME_BLOCK_SHIFT] + 
                    Column->Deltas[Sample]
                );
            }
        }

        return;
    }

    if (ToF64) {
        switch (Column->Kind) {
            default: {
                FLVTempSetMissing(Lane, Dst);

                return;
            } break;

            case FLI_SCALAR_KIND_U8: {
                FLV_LOAD_LOOP(u8,  f64);
            } break;

            case FLI_SCALAR_KIND_I8: {
                FLV_LOAD_LOOP(i8,  f64);
            } break;

            case FLI_SCALAR_KIND_U16: {
                FLV_LOAD_LOOP(u16, f64);
            } break;

            case FLI_SCALAR_KIND_I16: {
                FLV_LOAD_LOOP(i16, f64);
            } break;

            case FLI_SCALAR_KIND_U32: {
                FLV_LOAD_LOOP(u32, f64);
            } break;

            case FLI_SCALAR_KIND_I32: {
                FLV_LOAD_LOOP(i32, f64);
            } break;

            case FLI_SCALAR_KIND_U64: {
                FLV_LOAD_LOOP(u64, f64);
            } break;

            case FLI_SCALAR_KIND_I64: {
                FLV_LOAD_LOOP(i64, f64);
            } break;

            case FLI_SCALAR_KIND_F32: {
                FLV_LOAD_LOOP(f32, f64);
            } break;

            case FLI_SCALAR_KIND_F64: {
                FLV_LOAD_LOOP(f64, f64);
            } break;
        }

        if (Column->Scale != 1.0) {
            f64* D = (f64*) Result;
            f64 Scale = Column->Scale;

            for (u64 Index = 0; Index < Count; ++Index)
                D[Index] *= Scale;
        }
    } else {
        switch (Column->Kind) {
            default: {
                FLVTempSetMissing(Lane, Dst);

                return;
            } break;

            case FLI_SCALAR_KIND_U8: {
                FLV_LOAD_LOOP(u8,  i64);
            } break;

            case FLI_SCALAR_KIND_I8: {
                FLV_LOAD_LOOP(i8,  i64);
            } break;

            case FLI_SCALAR_KIND_U16: {
                FLV_LOAD_LOOP(u16, i64);
            } break;

            case FLI_SCALAR_KIND_I16: {
                FLV_LOAD_LOOP(i16, i64);
            } break;

            case FLI_SCALAR_KIND_U32: {
                FLV_LOAD_LOOP(u32, i64);
            } break;

            case FLI_SCALAR_KIND_I32: {
                FLV_LOAD_LOOP(i32, i64);
            } break;

            case FLI_SCALAR_KIND_U64: {
                FLV_LOAD_LOOP(u64, i64);
            } break;

            case FLI_SCALAR_KIND_I64: {
                FLV_LOAD_LOOP(i64, i64);
            } break;

            case FLI_SCALAR_KIND_F32: {
                FLV_LOAD_LOOP(f32, i64);
            } break;

            case FLI_SCALAR_KIND_F64: {
                FLV_LOAD_LOOP(f64, i64);
            } break;
        }

        if (Column->Scale != 1.0) {
            i64* D = (i64*) Result;
            f64 Scale = Column->Scale;

            for (u64 Index = 0; Index < Count; ++Index)
                D[Index] = (i64) ((f64) D[Index] * Scale);
        }
    }
}
#undef FLV_LOAD_LOOP

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelLOAD_I64)
{
    ASSERT(FLVGetOperandKind(Op.LHS) == FLV_OPERAND_KIND_SLOT);

    FLVLoadChunk(
        Lane, 
        &Lane->Columns[FLVGetOperandIndex(Op.LHS)], 
        Op.Dst, 
        FALSE
    );
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelLOAD_F64)
{
    ASSERT(FLVGetOperandKind(Op.LHS) == FLV_OPERAND_KIND_SLOT);

    FLVLoadChunk(
        Lane, 
        &Lane->Columns[FLVGetOperandIndex(Op.LHS)], 
        Op.Dst, 
        TRUE
    );
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelLOAD_INSTANCE)
{
    FLVLoadChunk(
        Lane,
        &Lane->InstanceColumns[Lane->Region->BaseSlot],
        Op.Dst,
        FALSE
    );
}

#define FLV_HOLD_LOOP(SrcType, DstType)                       \
    do {                                                      \
        SrcType const* __Src = (SrcType const*) Column->Data; \
        DstType* __Dst = (DstType*) Result;                   \
                                                              \
        for (u64 Index = 0; Index < Count; ++Index)           \
            __Dst[Index] = (DstType) __Src[Held[Index]];      \
    } while (FALSE)

#define FLV_HOLD_SWITCH(DstType)                                          \
    switch (Column->Kind) {                                               \
        default: {                                                        \
            FLVTempSetMissing(Lane, Dst);                                 \
                                                                          \
            return;                                                       \
        } break;                                                          \
                                                                          \
        case FLI_SCALAR_KIND_U8:  { FLV_HOLD_LOOP(u8,  DstType); } break; \
        case FLI_SCALAR_KIND_I8:  { FLV_HOLD_LOOP(i8,  DstType); } break; \
        case FLI_SCALAR_KIND_U16: { FLV_HOLD_LOOP(u16, DstType); } break; \
        case FLI_SCALAR_KIND_I16: { FLV_HOLD_LOOP(i16, DstType); } break; \
        case FLI_SCALAR_KIND_U32: { FLV_HOLD_LOOP(u32, DstType); } break; \
        case FLI_SCALAR_KIND_I32: { FLV_HOLD_LOOP(i32, DstType); } break; \
        case FLI_SCALAR_KIND_U64: { FLV_HOLD_LOOP(u64, DstType); } break; \
        case FLI_SCALAR_KIND_I64: { FLV_HOLD_LOOP(i64, DstType); } break; \
        case FLI_SCALAR_KIND_F32: { FLV_HOLD_LOOP(f32, DstType); } break; \
        case FLI_SCALAR_KIND_F64: { FLV_HOLD_LOOP(f64, DstType); } break; \
    }

INTERNAL void
FLVHoldChunk(FLVLane* Lane, u32 Slot, FLVOperand Dst, b32 ToF64)
{
    FLVBoundSlot const* Bound = &Lane->Slots[Slot];
    FLVColumn const* Column = &Lane->Columns[Slot];
    FLVColumn const* Time = &Lane->TimeColumns[Slot];
    FLVColumn const* Instance = &Lane->InstanceColumns[Slot];
    FLVHold* Hold = &Lane->Holds[FLVGetOperandIndex(Dst)];
    void* Result = FLVTempPtr(Lane, Dst);
    u64 const* T = Lane->ChunkTime;
    u64* Held = Lane->ChunkHold;
    u64 Count = Lane->ChunkCount;
    u64 End = Time->Count;
    b32 HasInstance = (Bound->Instance != FLV_INSTANCE_ANY);
    f64 Wanted = (f64) Bound->Instance;

    if (
        Bound->IsMissing ||
        Column->IsMissing ||
        Time->IsMissing ||
        !End ||
        (
            HasInstance && 
            Instance->IsMissing
        )
    ) {
        FLVTempSetMissing(Lane, Dst);

        return;
    }

    u64 Cursor = Hold->Cursor;
    u64 Last = Hold->Held;
    b32 HasHeld = Hold->HasHeld;
    u64 HoleCount = 0;

    for (u64 Index = 0; Index < Count; ++Index) {
        while (Cursor < End && FLVTimeAt(Time, Cursor) <= T[Index]) {
            if (
                !HasInstance || 
                FLVColumnF64At(Instance, Cursor) == Wanted
            ) {
                Last = Cursor;
                HasHeld = TRUE;
            }

            ++Cursor;
        }

        Held[Index] = Last;
        HoleCount += !HasHeld;
    }

    Hold->Cursor = Cursor;
    Hold->Held = Last;
    Hold->HasHeld = HasHeld;

    if (ToF64) {
        f64* D = (f64*) Result;

        if (Column->Encoding == FLI_COLUMN_ENCODING_CONSTANT) {
            for (u64 Index = 0; Index < Count; ++Index)
                D[Index] = Column->Constant;
        } else if (Column->Encoding == FLI_COLUMN_ENCODING_DELTA_TIME) {
            for (u64 Index = 0; Index < Count; ++Index)
                D[Index] = (f64) FLVTimeAt(Column, Held[Index]);
        } else {
            FLV_HOLD_SWITCH(f64);

            if (Column->Scale != 1.0) {
                f64 Scale = Column->Scale;

                for (u64 Index = 0; Index < Count; ++Index)
                    D[Index] *= Scale;
            }
        }

        for (u64 Index = 0; Index < HoleCount; ++Index)
            D[Index] = NC_NAN;
    } else {
        i64* D = (i64*) Result;

        if (Column->Encoding == FLI_COLUMN_ENCODING_CONSTANT) {
            for (u64 Index = 0; Index < Count; ++Index)
                D[Index] = (i64) Column->Constant;
        } else if (Column->Encoding == FLI_COLUMN_ENCODING_DELTA_TIME) {
            for (u64 Index = 0; Index < Count; ++Index)
                D[Index] = (i64) FLVTimeAt(Column, Held[Index]);
        } else {
            FLV_HOLD_SWITCH(i64);

            if (Column->Scale != 1.0) {
                f64 Scale = Column->Scale;

                for (u64 Index = 0; Index < Count; ++Index)
                    D[Index] = (i64) ((f64) D[Index] * Scale);
            }
        }

        for (u64 Index = 0; Index < HoleCount; ++Index)
            D[Index] = FLV_I64_HOLE;
    }
}
#undef FLV_HOLD_SWITCH
#undef FLV_HOLD_LOOP

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelLOAD_HOLD_I64)
{
    ASSERT(FLVGetOperandKind(Op.LHS) == FLV_OPERAND_KIND_SLOT);

    FLVHoldChunk(Lane, FLVGetOperandIndex(Op.LHS), Op.Dst, FALSE);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelLOAD_HOLD_F64)
{
    ASSERT(FLVGetOperandKind(Op.LHS) == FLV_OPERAND_KIND_SLOT);

    FLVHoldChunk(Lane, FLVGetOperandIndex(Op.LHS), Op.Dst, TRUE);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelCONVERT_I64_F64)
{
    if (FLVTempIsMissing(Lane, Op.LHS)) {
        FLVTempSetMissing(Lane, Op.Dst);
 
        return;
    }
 
    i64 const* A = (i64 const*) FLVTempPtr(Lane, Op.LHS);
    f64* Dst = (f64*) FLVTempPtr(Lane, Op.Dst);
 
    for (u64 Index = 0; Index < Lane->ChunkCount; ++Index)
        Dst[Index] = (A[Index] == FLV_I64_HOLE) ? NC_NAN : (f64) A[Index];
}

#if FLV_KERNEL_SIMD
    #define FLV_WIDE_TYPE_F64         v256d
    #define FLV_WIDE_TYPE_I64         v256
    #define FLV_WIDE_LOAD_F64(P)      _mm256_loadu_pd((f64 const*) (P))
    #define FLV_WIDE_LOAD_I64(P)      _mm256_loadu_si256((v256 const*) (P))
    #define FLV_WIDE_STORE_F64(P, V)  _mm256_storeu_pd((f64*) (P), (V))
    #define FLV_WIDE_STORE_I64(P, V)  _mm256_storeu_si256((v256*) (P), (V))
    #define FLV_WIDE_SET_F64(X)       _mm256_set1_pd(X)
    #define FLV_WIDE_SET_I64(X)       _mm256_set1_epi64x(X)
    #define FLV_WIDE_BITS_F64(V)      ((u64) _mm256_movemask_pd(V))
    #define FLV_WIDE_BITS_I64(V)      ((u64) _mm256_movemask_pd(_mm256_castsi256_pd(V)))
    #define FLV_WIDE_ADD_F64(A, B)    _mm256_add_pd((A), (B))
    #define FLV_WIDE_SUB_F64(A, B)    _mm256_sub_pd((A), (B))
    #define FLV_WIDE_MUL_F64(A, B)    _mm256_mul_pd((A), (B))
    #define FLV_WIDE_DIV_F64(A, B)    _mm256_div_pd((A), (B))
    #define FLV_WIDE_MOD_F64(A, B)    FLVWideModF64((A), (B))
    #define FLV_WIDE_ADD_I64(A, B)    _mm256_add_epi64((A), (B))
    #define FLV_WIDE_SUB_I64(A, B)    _mm256_sub_epi64((A), (B))
    #define FLV_WIDE_CMP_LT_F64(A, B) FLV_WIDE_BITS_F64(_mm256_cmp_pd((A), (B), _CMP_LT_OQ))
    #define FLV_WIDE_CMP_LE_F64(A, B) FLV_WIDE_BITS_F64(_mm256_cmp_pd((A), (B), _CMP_LE_OQ))
    #define FLV_WIDE_CMP_GT_F64(A, B) FLV_WIDE_BITS_F64(_mm256_cmp_pd((A), (B), _CMP_GT_OQ))
    #define FLV_WIDE_CMP_GE_F64(A, B) FLV_WIDE_BITS_F64(_mm256_cmp_pd((A), (B), _CMP_GE_OQ))
    #define FLV_WIDE_CMP_EQ_F64(A, B) FLV_WIDE_BITS_F64(_mm256_cmp_pd((A), (B), _CMP_EQ_OQ))
    #define FLV_WIDE_CMP_NE_F64(A, B) FLV_WIDE_BITS_F64(_mm256_cmp_pd((A), (B), _CMP_NEQ_UQ))
    #define FLV_WIDE_CMP_LT_I64(A, B) FLV_WIDE_BITS_I64(_mm256_cmpgt_epi64((B), (A)))
    #define FLV_WIDE_CMP_LE_I64(A, B) (FLV_WIDE_BITS_I64(_mm256_cmpgt_epi64((A), (B))) ^ 0xF)
    #define FLV_WIDE_CMP_GT_I64(A, B) FLV_WIDE_BITS_I64(_mm256_cmpgt_epi64((A), (B)))
    #define FLV_WIDE_CMP_GE_I64(A, B) (FLV_WIDE_BITS_I64(_mm256_cmpgt_epi64((B), (A))) ^ 0xF)
    #define FLV_WIDE_CMP_EQ_I64(A, B) FLV_WIDE_BITS_I64(_mm256_cmpeq_epi64((A), (B)))
    #define FLV_WIDE_CMP_NE_I64(A, B) (FLV_WIDE_BITS_I64(_mm256_cmpeq_epi64((A), (B))) ^ 0xF)

    // NOTE(nc): the same steps as FMod, so the value is the same one
    INTERNAL v256d
    FLVWideModF64(v256d A, v256d B)
    {
        v256d Div = _mm256_div_pd(A, B);
        v256d Trunc = _mm256_round_pd(Div, _MM_FROUND_TRUNC);
        v256d Result = _mm256_sub_pd(A, _mm256_mul_pd(Trunc, B));

        return Result;
    }

    // NOTE(nc): a lane of the result is all ones if its bit of the low four
    //         : bits is set
    INTERNAL v256
    FLVWideLaneMask(u64 Bits)
    {
        v256 Select = _mm256_setr_epi64x(1, 2, 4, 8);
        v256 Result = _mm256_cmpeq_epi64(
            _mm256_and_si256(_mm256_set1_epi64x((i64) Bits), Select),
            Select
        );

        return Result;
    }

    // NOTE(nc): four samples of a word of a reduction. a sample that is not
    //         : in the reduction is read as Other.
    #define FLV_WIDE_LIVE_F64(X, Live, At, Other)                \
        _mm256_blendv_pd(                                        \
            (Other),                                             \
            _mm256_loadu_pd((X) + (At)),                         \
            _mm256_castsi256_pd(FLVWideLaneMask((Live) >> (At))) \
        )

    #define FLV_WIDE_LIVE_I64(X, Live, At, Other)           \
        _mm256_blendv_epi8(                                 \
            (Other),                                        \
            _mm256_loadu_si256((v256 const*) ((X) + (At))), \
            FLVWideLaneMask((Live) >> (At))                 \
        )

    INTERNAL v256
    FLVWideMinI64(v256 A, v256 B)
    {
        v256 Result = _mm256_blendv_epi8(A, B, _mm256_cmpgt_epi64(A, B));

        return Result;
    }

    INTERNAL v256
    FLVWideMaxI64(v256 A, v256 B)
    {
        v256 Result = _mm256_blendv_epi8(A, B, _mm256_cmpgt_epi64(B, A));

        return Result;
    }

    // NOTE(nc): a kernel writes whole words of a mask, so the bits after the
    //         : last sample of the chunk are cleared here. the rest of the
    //         : kernels take them to be 0.
    INTERNAL void
    FLVMaskTrim(u64* Mask, u64 Count)
    {
        u64 Words = (Count + 63) >> 6;
        u64 TailBits = Count & 63;

        if (TailBits)
            Mask[Words - 1] &= (1ULL << TailBits) - 1;

        for (u64 Word = Words; Word < FLV_CHUNK_MASK_WORDS; ++Word)
            Mask[Word] = 0;
    }

    // NOTE(nc): the bits of one word of the filter whose samples have a
    //         : value. A is the 64 samples of the word.
    INTERNAL u64
    FLVLiveWordF64(f64 const* A, u64 MaskWord)
    {
        u64 Valid = 0;

        for (u64 Offset = 0; Offset < 64; Offset += FLV_WIDE) {
            v256d X = _mm256_loadu_pd(A + Offset);

            Valid |= FLV_WIDE_BITS_F64(_mm256_cmp_pd(X, X, _CMP_ORD_Q)) << Offset;
        }

        return MaskWord & Valid;
    }

    INTERNAL u64
    FLVLiveWordI64(i64 const* A, u64 MaskWord)
    {
        v256 Hole = _mm256_set1_epi64x(FLV_I64_HOLE);
        u64 Holes = 0;

        for (u64 Offset = 0; Offset < 64; Offset += FLV_WIDE) {
            v256 X = _mm256_loadu_si256((v256 const*) (A + Offset));

            Holes |= FLV_WIDE_BITS_I64(_mm256_cmpeq_epi64(X, Hole)) << Offset;
        }

        return MaskWord & ~Holes;
    }
#endif

#define FLV_ARITH_KERNEL_T(Name, Type, OpM)                     \
    INTERNAL                                                    \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                        \
    {                                                           \
        FLV_ARITH_HEAD(Type);                                   \
        Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS); \
        Type const* B = (Type const*) FLVTempPtr(Lane, Op.RHS); \
                                                                \
        for (u64 Index = 0; Index < Count; ++Index)             \
            Dst[Index] = OpM(A[Index], B[Index]);               \
    }
 
#define FLV_ARITH_KERNEL_S(Name, Type, ScalarFunc, OpM)         \
    INTERNAL                                                    \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                        \
    {                                                           \
        FLV_ARITH_HEAD(Type);                                   \
        Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS); \
        Type        B = ScalarFunc(Lane, Op.RHS);               \
                                                                \
        for (u64 Index = 0; Index < Count; ++Index)             \
            Dst[Index] = OpM(A[Index], B);                      \
    }
 
#define FLV_ARITH_KERNEL_RS(Name, Type, ScalarFunc, OpM)        \
    INTERNAL                                                    \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                        \
    {                                                           \
        FLV_ARITH_HEAD(Type);                                   \
        Type A = ScalarFunc(Lane, Op.LHS);                      \
        Type const* B = (Type const*) FLVTempPtr(Lane, Op.RHS); \
                                                                \
        for (u64 Index = 0; Index < Count; ++Index)             \
            Dst[Index] = OpM(A, B[Index]);                      \
    }
 
#if FLV_KERNEL_SIMD
    // NOTE(nc): a temp has FLV_CHUNK_SAMPLES samples, so a wide kernel runs
    //         : to the end of the last group of FLV_WIDE and has no tail. the
    //         : samples after ChunkCount have no meaning and no kernel reads
    //         : them as values.
    #define FLV_ARITH_WIDE_KERNEL_T(Name, Type, Kind, OpM)                \
        INTERNAL                                                          \
        FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                              \
        {                                                                 \
            FLV_ARITH_HEAD(Type);                                         \
            Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);       \
            Type const* B = (Type const*) FLVTempPtr(Lane, Op.RHS);       \
                                                                          \
            for (u64 Index = 0; Index < Count; Index += FLV_WIDE) {       \
                FLV_WIDE_TYPE_##Kind X = FLV_WIDE_LOAD_##Kind(A + Index); \
                FLV_WIDE_TYPE_##Kind Y = FLV_WIDE_LOAD_##Kind(B + Index); \
                                                                          \
                FLV_WIDE_STORE_##Kind(Dst + Index, OpM(X, Y));            \
            }                                                             \
        }
    
    #define FLV_ARITH_WIDE_KERNEL_S(Name, Type, Kind, ScalarFunc, OpM)    \
        INTERNAL                                                          \
        FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                              \
        {                                                                 \
            FLV_ARITH_HEAD(Type);                                         \
            Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);       \
            FLV_WIDE_TYPE_##Kind Y = FLV_WIDE_SET_##Kind(                 \
                ScalarFunc(Lane, Op.RHS)                                  \
            );                                                            \
                                                                          \
            for (u64 Index = 0; Index < Count; Index += FLV_WIDE) {       \
                FLV_WIDE_TYPE_##Kind X = FLV_WIDE_LOAD_##Kind(A + Index); \
                                                                          \
                FLV_WIDE_STORE_##Kind(Dst + Index, OpM(X, Y));            \
            }                                                             \
        }
    
    #define FLV_ARITH_WIDE_KERNEL_RS(Name, Type, Kind, ScalarFunc, OpM)   \
        INTERNAL                                                          \
        FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                              \
        {                                                                 \
            FLV_ARITH_HEAD(Type);                                         \
            FLV_WIDE_TYPE_##Kind X = FLV_WIDE_SET_##Kind(                 \
                ScalarFunc(Lane, Op.LHS)                                  \
            );                                                            \
            Type const* B = (Type const*) FLVTempPtr(Lane, Op.RHS);       \
                                                                          \
            for (u64 Index = 0; Index < Count; Index += FLV_WIDE) {       \
                FLV_WIDE_TYPE_##Kind Y = FLV_WIDE_LOAD_##Kind(B + Index); \
                                                                          \
                FLV_WIDE_STORE_##Kind(Dst + Index, OpM(X, Y));            \
            }                                                             \
        }
    
    #define FLV_ARITH_WIDE_FAMILY_2(Name, Type, Kind, ScalarFunc, OpM) \
        FLV_ARITH_WIDE_KERNEL_T(Name##_T, Type, Kind, OpM)             \
        FLV_ARITH_WIDE_KERNEL_S(Name##_S, Type, Kind, ScalarFunc, OpM)
    
    #define FLV_ARITH_WIDE_FAMILY_3(Name, Type, Kind, ScalarFunc, OpM) \
        FLV_ARITH_WIDE_KERNEL_T(Name##_T, Type, Kind, OpM)             \
        FLV_ARITH_WIDE_KERNEL_S(Name##_S, Type, Kind, ScalarFunc, OpM) \
        FLV_ARITH_WIDE_KERNEL_RS(Name##_RS, Type, Kind, ScalarFunc, OpM)
    
    FLV_ARITH_WIDE_FAMILY_2(ADD_I64, i64, I64, FLVScalarI64, FLV_WIDE_ADD_I64)
    FLV_ARITH_WIDE_FAMILY_2(ADD_F64, f64, F64, FLVScalarF64, FLV_WIDE_ADD_F64)
    FLV_ARITH_WIDE_FAMILY_3(SUB_I64, i64, I64, FLVScalarI64, FLV_WIDE_SUB_I64)
    FLV_ARITH_WIDE_FAMILY_3(SUB_F64, f64, F64, FLVScalarF64, FLV_WIDE_SUB_F64)
    FLV_ARITH_WIDE_FAMILY_2(MUL_F64, f64, F64, FLVScalarF64, FLV_WIDE_MUL_F64)
    FLV_ARITH_WIDE_FAMILY_3(DIV_F64, f64, F64, FLVScalarF64, FLV_WIDE_DIV_F64)
    FLV_ARITH_WIDE_FAMILY_3(MOD_F64, f64, F64, FLVScalarF64, FLV_WIDE_MOD_F64)
    
    // NOTE(nc): AVX2 has no multiply and no divide of i64, so these are the
    //         : scalar kernels
    FLV_ARITH_KERNEL_T(MUL_I64_T, i64, FLV_OP_MUL)
    FLV_ARITH_KERNEL_S(MUL_I64_S, i64, FLVScalarI64, FLV_OP_MUL)
    FLV_ARITH_KERNEL_T(DIV_I64_T, i64, FLV_OP_DIV_I64)
    FLV_ARITH_KERNEL_S(DIV_I64_S, i64, FLVScalarI64, FLV_OP_DIV_I64)
    FLV_ARITH_KERNEL_RS(DIV_I64_RS, i64, FLVScalarI64, FLV_OP_DIV_I64)
    FLV_ARITH_KERNEL_T(MOD_I64_T, i64, FLV_OP_MOD_I64)
    FLV_ARITH_KERNEL_S(MOD_I64_S, i64, FLVScalarI64, FLV_OP_MOD_I64)
    FLV_ARITH_KERNEL_RS(MOD_I64_RS, i64, FLVScalarI64, FLV_OP_MOD_I64)
    
    #undef FLV_ARITH_WIDE_FAMILY_3
    #undef FLV_ARITH_WIDE_FAMILY_2
    #undef FLV_ARITH_WIDE_KERNEL_RS
    #undef FLV_ARITH_WIDE_KERNEL_S
    #undef FLV_ARITH_WIDE_KERNEL_T
#else
    FLV_ARITH_FAMILY_2(ADD, FLV_OP_ADD, FLV_OP_ADD)
    FLV_ARITH_FAMILY_3(SUB, FLV_OP_SUB, FLV_OP_SUB)
    FLV_ARITH_FAMILY_2(MUL, FLV_OP_MUL, FLV_OP_MUL)
    FLV_ARITH_FAMILY_3(DIV, FLV_OP_DIV_I64, FLV_OP_DIV_F64)
    FLV_ARITH_FAMILY_3(MOD, FLV_OP_MOD_I64, FLV_OP_MOD_F64)
#endif
 
#undef FLV_ARITH_FAMILY_3
#undef FLV_ARITH_FAMILY_2
#undef FLV_ARITH_KERNEL_RS
#undef FLV_ARITH_KERNEL_S
#undef FLV_ARITH_KERNEL_T
#undef FLV_ARITH_HEAD
 
#define FLV_UNARY_KERNEL(Name, SrcType, DstType, Expr)                \
    INTERNAL                                                          \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                              \
    {                                                                 \
        if (FLVTempIsMissing(Lane, Op.LHS)) {                         \
            FLVTempSetMissing(Lane, Op.Dst);                          \
                                                                      \
            return;                                                   \
        }                                                             \
                                                                      \
        SrcType const* A = (SrcType const*) FLVTempPtr(Lane, Op.LHS); \
        DstType* Dst = (DstType*) FLVTempPtr(Lane, Op.Dst);           \
                                                                      \
        for (u64 Index = 0; Index < Lane->ChunkCount; ++Index)        \
            Dst[Index] = (Expr);                                      \
    }
 
#if FLV_KERNEL_SIMD
    #define FLV_UNARY_WIDE_KERNEL(Name, Type, Kind, Expr)                      \
        INTERNAL                                                               \
        FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                   \
        {                                                                      \
            if (FLVTempIsMissing(Lane, Op.LHS)) {                              \
                FLVTempSetMissing(Lane, Op.Dst);                               \
                                                                               \
                return;                                                        \
            }                                                                  \
                                                                               \
            Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);            \
            Type* Dst = (Type*) FLVTempPtr(Lane, Op.Dst);                      \
                                                                               \
            for (u64 Index = 0; Index < Lane->ChunkCount; Index += FLV_WIDE) { \
                FLV_WIDE_TYPE_##Kind X = FLV_WIDE_LOAD_##Kind(A + Index);      \
                                                                               \
                FLV_WIDE_STORE_##Kind(Dst + Index, (Expr));                    \
            }                                                                  \
        }
    
    // NOTE(nc): an abs is a blend on X < 0, as the scalar one is, so a -0.0
    //         : and a NaN come out with the sign they went in with
    FLV_UNARY_WIDE_KERNEL(
        NEG_I64, i64, I64,
        _mm256_sub_epi64(_mm256_setzero_si256(), X)
    )
    FLV_UNARY_WIDE_KERNEL(
        NEG_F64, f64, F64,
        _mm256_xor_pd(X, _mm256_set1_pd(-0.0))
    )
    FLV_UNARY_WIDE_KERNEL(
        ABS_I64, i64, I64,
        _mm256_blendv_epi8(
            X,
            _mm256_sub_epi64(_mm256_setzero_si256(), X),
            _mm256_cmpgt_epi64(_mm256_setzero_si256(), X)
        )
    )
    FLV_UNARY_WIDE_KERNEL(
        ABS_F64, f64, F64,
        _mm256_blendv_pd(
            X,
            _mm256_xor_pd(X, _mm256_set1_pd(-0.0)),
            _mm256_cmp_pd(X, _mm256_setzero_pd(), _CMP_LT_OQ)
        )
    )
    FLV_UNARY_WIDE_KERNEL(
        SQRT_F64, f64, F64,
        _mm256_sqrt_pd(X)
    )
    
    #undef FLV_UNARY_WIDE_KERNEL
#else
    FLV_UNARY_KERNEL(NEG_I64, i64, i64, -A[Index])
    FLV_UNARY_KERNEL(NEG_F64, f64, f64, -A[Index])
    FLV_UNARY_KERNEL(ABS_I64, i64, i64, (A[Index] < 0) ? -A[Index] : A[Index])
    FLV_UNARY_KERNEL(ABS_F64, f64, f64, (A[Index] < 0.0) ? -A[Index] : A[Index])
    FLV_UNARY_KERNEL(SQRT_F64, f64, f64, SqrtF64(A[Index]))
#endif
 
#undef FLV_UNARY_KERNEL
#undef FLV_OP_MOD_F64
#undef FLV_OP_MOD_I64
#undef FLV_OP_DIV_F64
#undef FLV_OP_DIV_I64
#undef FLV_OP_MUL
#undef FLV_OP_SUB
#undef FLV_OP_ADD

#define FLV_CMP_KERNEL_T(Name, Type, OpSym)                                      \
    INTERNAL                                                                     \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                         \
    {                                                                            \
        if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {  \
            FLVTempSetMissing(Lane, Op.Dst);                                     \
                                                                                 \
            return;                                                              \
        }                                                                        \
                                                                                 \
        Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);                  \
        Type const* B = (Type const*) FLVTempPtr(Lane, Op.RHS);                  \
        u64* Mask = (u64*) FLVTempPtr(Lane, Op.Dst);                             \
                                                                                 \
        MemSet(Mask, 0, FLV_CHUNK_MASK_WORDS * sizeof(u64));                     \
                                                                                 \
        for (u64 Index = 0; Index < Lane->ChunkCount; ++Index)                   \
            Mask[Index >> 6] |= (u64) (A[Index] OpSym B[Index]) << (Index & 63); \
    }

#define FLV_CMP_KERNEL_S(Name, Type, OpSym, ScalarFunc)                         \
    INTERNAL                                                                    \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                        \
    {                                                                           \
        if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) { \
            FLVTempSetMissing(Lane, Op.Dst);                                    \
                                                                                \
            return;                                                             \
        }                                                                       \
                                                                                \
        Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);                 \
        Type B = ScalarFunc(Lane, Op.RHS);                                      \
        u64* Mask = (u64*) FLVTempPtr(Lane, Op.Dst);                            \
                                                                                \
        MemSet(Mask, 0, FLV_CHUNK_MASK_WORDS * sizeof(u64));                    \
                                                                                \
        for (u64 Index = 0; Index < Lane->ChunkCount; ++Index)                  \
            Mask[Index >> 6] |= (u64) (A[Index] OpSym B) << (Index & 63);       \
    }

#define FLV_CMP_FAMILY(Cmp, OpSym)                          \
    FLV_CMP_KERNEL_T(Cmp##_I64_T, i64, OpSym)               \
    FLV_CMP_KERNEL_S(Cmp##_I64_S, i64, OpSym, FLVScalarI64) \
    FLV_CMP_KERNEL_T(Cmp##_F64_T, f64, OpSym)               \
    FLV_CMP_KERNEL_S(Cmp##_F64_S, f64, OpSym, FLVScalarF64)

#if FLV_KERNEL_SIMD
    // NOTE(nc): a compare of four samples is four bits, and 16 of them are a
    //         : word of the mask
    #define FLV_CMP_WIDE_KERNEL_T(Name, Type, Kind, CmpM)                           \
        INTERNAL                                                                    \
        FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                        \
        {                                                                           \
            if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) { \
                FLVTempSetMissing(Lane, Op.Dst);                                    \
                                                                                    \
                return;                                                             \
            }                                                                       \
                                                                                    \
            Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);                 \
            Type const* B = (Type const*) FLVTempPtr(Lane, Op.RHS);                 \
            u64* Mask = (u64*) FLVTempPtr(Lane, Op.Dst);                            \
            u64 Words = (Lane->ChunkCount + 63) >> 6;                               \
                                                                                    \
            for (u64 Word = 0; Word < Words; ++Word) {                              \
                Type const* WordA = A + (Word << 6);                                \
                Type const* WordB = B + (Word << 6);                                \
                u64 Bits = 0;                                                       \
                                                                                    \
                for (u64 Offset = 0; Offset < 64; Offset += FLV_WIDE) {             \
                    FLV_WIDE_TYPE_##Kind X = FLV_WIDE_LOAD_##Kind(WordA + Offset);  \
                    FLV_WIDE_TYPE_##Kind Y = FLV_WIDE_LOAD_##Kind(WordB + Offset);  \
                                                                                    \
                    Bits |= CmpM(X, Y) << Offset;                                   \
                }                                                                   \
                                                                                    \
                Mask[Word] = Bits;                                                  \
            }                                                                       \
                                                                                    \
            FLVMaskTrim(Mask, Lane->ChunkCount);                                    \
        }
    
    #define FLV_CMP_WIDE_KERNEL_S(Name, Type, Kind, CmpM, ScalarFunc)               \
        INTERNAL                                                                    \
        FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                        \
        {                                                                           \
            if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) { \
                FLVTempSetMissing(Lane, Op.Dst);                                    \
                                                                                    \
                return;                                                             \
            }                                                                       \
                                                                                    \
            Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);                 \
            FLV_WIDE_TYPE_##Kind Y = FLV_WIDE_SET_##Kind(                           \
                ScalarFunc(Lane, Op.RHS)                                            \
            );                                                                      \
            u64* Mask = (u64*) FLVTempPtr(Lane, Op.Dst);                            \
            u64 Words = (Lane->ChunkCount + 63) >> 6;                               \
                                                                                    \
            for (u64 Word = 0; Word < Words; ++Word) {                              \
                Type const* WordA = A + (Word << 6);                                \
                u64 Bits = 0;                                                       \
                                                                                    \
                for (u64 Offset = 0; Offset < 64; Offset += FLV_WIDE) {             \
                    FLV_WIDE_TYPE_##Kind X = FLV_WIDE_LOAD_##Kind(WordA + Offset);  \
                                                                                    \
                    Bits |= CmpM(X, Y) << Offset;                                   \
                }                                                                   \
                                                                                    \
                Mask[Word] = Bits;                                                  \
            }                                                                       \
                                                                                    \
            FLVMaskTrim(Mask, Lane->ChunkCount);                                    \
        }
    
    #define FLV_CMP_WIDE_FAMILY(Cmp)                                                     \
        FLV_CMP_WIDE_KERNEL_T(Cmp##_I64_T, i64, I64, FLV_WIDE_##Cmp##_I64)               \
        FLV_CMP_WIDE_KERNEL_S(Cmp##_I64_S, i64, I64, FLV_WIDE_##Cmp##_I64, FLVScalarI64) \
        FLV_CMP_WIDE_KERNEL_T(Cmp##_F64_T, f64, F64, FLV_WIDE_##Cmp##_F64)               \
        FLV_CMP_WIDE_KERNEL_S(Cmp##_F64_S, f64, F64, FLV_WIDE_##Cmp##_F64, FLVScalarF64)
    
    FLV_CMP_WIDE_FAMILY(CMP_LT)
    FLV_CMP_WIDE_FAMILY(CMP_LE)
    FLV_CMP_WIDE_FAMILY(CMP_GT)
    FLV_CMP_WIDE_FAMILY(CMP_GE)
    FLV_CMP_WIDE_FAMILY(CMP_EQ)
    FLV_CMP_WIDE_FAMILY(CMP_NE)
    
    #undef FLV_CMP_WIDE_FAMILY
    #undef FLV_CMP_WIDE_KERNEL_S
    #undef FLV_CMP_WIDE_KERNEL_T
#else
    FLV_CMP_FAMILY(CMP_LT, <)
    FLV_CMP_FAMILY(CMP_LE, <=)
    FLV_CMP_FAMILY(CMP_GT, >)
    FLV_CMP_FAMILY(CMP_GE, >=)
    FLV_CMP_FAMILY(CMP_EQ, ==)
    FLV_CMP_FAMILY(CMP_NE, !=)
#endif

#undef FLV_CMP_FAMILY
#undef FLV_CMP_KERNEL_S
#undef FLV_CMP_KERNEL_T

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelMASK_AND)
{
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    u64 const* LHS = (u64 const*) FLVTempPtr(Lane, Op.LHS);
    u64 const* RHS = (u64 const*) FLVTempPtr(Lane, Op.RHS);
    u64* Dst = (u64*) FLVTempPtr(Lane, Op.Dst);

    for (u64 Index = 0; Index < FLV_CHUNK_MASK_WORDS; ++Index)
        Dst[Index] = LHS[Index] & RHS[Index];
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelMASK_OR)
{
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    u64 const* LHS = (u64 const*) FLVTempPtr(Lane, Op.LHS);
    u64 const* RHS = (u64 const*) FLVTempPtr(Lane, Op.RHS);
    u64*       Dst = (u64*) FLVTempPtr(Lane, Op.Dst);

    for (u64 Index = 0; Index < FLV_CHUNK_MASK_WORDS; ++Index)
        Dst[Index] = LHS[Index] | RHS[Index];
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelMASK_NOT)
{
    if (FLVTempIsMissing(Lane, Op.LHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    u64 const* LHS = (u64 const*) FLVTempPtr(Lane, Op.LHS);
    u64*       Dst = (u64*) FLVTempPtr(Lane, Op.Dst);
    u64        FullWords = Lane->ChunkCount >> 6;
    u64        TailBits = Lane->ChunkCount & 63;

    for (u64 Index = 0; Index < FullWords; ++Index)
        Dst[Index] = ~LHS[Index];

    if (TailBits)
        Dst[FullWords] = ~LHS[FullWords] & ((1ULL << TailBits) - 1);

    for (
        u64 Index = FullWords + !!TailBits;
        Index < FLV_CHUNK_MASK_WORDS;
        ++Index
    ) {
        Dst[Index] = 0;
    }
}

INTERNAL b32
FLVStringHas(u8 const* Text, u64 TextSize, Str8 Needle)
{
    b32 Result = !Needle.Size;

    for (
        u64 Start = 0;
        !Result && Start + Needle.Size <= TextSize;
        ++Start
    ) {
        Result = !!MemCmp(Text + Start, Needle.Str, Needle.Size);
    }

    return Result;
}

// NOTE(nc): the mask of the elements of a string column that match a
//         : string of the program. the text of an element is its bytes up
//         : to the first 0, or all of them. the match is exact, so the
//         : case matters. the column is of the base of the region.
INTERNAL void
FLVStringMask(FLVLane* Lane, FLVOp Op, b32 IsWhole, b32 IsNot)
{
    ASSERT(FLVGetOperandKind(Op.LHS) == FLV_OPERAND_KIND_SLOT);
    ASSERT(FLVGetOperandKind(Op.RHS) == FLV_OPERAND_KIND_CONST);

    FLVColumn const* Column = &Lane->Columns[FLVGetOperandIndex(Op.LHS)];
    Str8 Needle = Lane->Program->Strings[
        Lane->Program->Consts[FLVGetOperandIndex(Op.RHS)].I64
    ];
    u64* Mask = (u64*) FLVTempPtr(Lane, Op.Dst);
    u64 Size = Column->ElementSize;

    if (
        Column->IsMissing ||
        Column->Encoding != FLI_COLUMN_ENCODING_RAW ||
        Column->Kind < FLI_SCALAR_KIND_CHAR4 ||
        Column->Kind > FLI_SCALAR_KIND_CHAR64
    ) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    u8 const* Element = (u8 const*) Column->Data + Lane->ChunkBase * Size;

    MemSet(Mask, 0, FLV_CHUNK_MASK_WORDS * sizeof(u64));

    for (u64 Index = 0; Index < Lane->ChunkCount; ++Index, Element += Size) {
        u64 TextSize = 0;
        b32 IsMatch = FALSE;

        for ( ; TextSize < Size && Element[TextSize]; ++TextSize);

        if (!IsWhole || TextSize == Needle.Size)
            IsMatch = FLVStringHas(Element, TextSize, Needle);

        Mask[Index >> 6] |= (u64) (IsMatch != IsNot) << (Index & 63);
    }
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSTR_EQ)
{
    FLVStringMask(Lane, Op, TRUE, FALSE);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSTR_NE)
{
    FLVStringMask(Lane, Op, TRUE, TRUE);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSTR_HAS)
{
    FLVStringMask(Lane, Op, FALSE, FALSE);
}

#define FLV_VALID_I64(X) ((X) != FLV_I64_HOLE)
#define FLV_VALID_F64(X) ((X) == (X))
#define FLV_FINITE_F64(X) (((X) - (X)) == 0.0)

// NOTE(nc): a sample with no value, a hole or a NaN, is not in a
//         : reduction, as a sample that the filter takes out is not. the
//         : first sample of the reduction is the first that is in it.
#define FLV_REDUCE_KERNEL(Name, Type, Field, Valid, Body)                 \
    INTERNAL                                                              \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                  \
    {                                                                     \
        FLVReduction* R = &Lane->Reductions[FLVGetOperandIndex(Op.Dst)];  \
                                                                          \
        if (FLVTempIsMissing(Lane, Op.LHS)) {                             \
            FLVTempSetMissing(Lane, Op.Dst);                              \
                                                                          \
            return;                                                       \
        }                                                                 \
                                                                          \
        u64 const* Mask = Lane->Mask ? Lane->Mask : Lane->MaskAll;        \
        Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);           \
        u64 Head = FLVMaskHead(Mask, Lane->ChunkCount);                   \
        u64 Tail = FLVMaskTail(Mask, Lane->ChunkCount);                   \
                                                                          \
        if (!R->Count) {                                                  \
            for (                                                         \
                ;                                                         \
                Head < Tail &&                                            \
                !(FLVMaskBit(Mask, Head) && Valid(A[Head]));              \
                ++Head                                                    \
            );                                                            \
                                                                          \
            if (Head < Tail) {                                            \
                R->Head.Field = A[Head];                                  \
                R->Minimum.Field = A[Head];                               \
                R->Maximum.Field = A[Head];                               \
                R->Shift = (f64) A[Head];                                 \
            }                                                             \
        }                                                                 \
                                                                          \
        for (u64 Index = Head; Index < Tail; ++Index) {                   \
            Type X = A[Index];                                            \
            u64  Bit = FLVMaskBit(Mask, Index) & (u64) Valid(X);          \
                                                                          \
            Body;                                                         \
            R->Tail.Field = Bit ? X : R->Tail.Field;                      \
            R->Count += Bit;                                              \
        }                                                                 \
    }

 
#define FLV_BODY_NONE

#define FLV_BODY_SUM(Field, Zero) R->Summation.Field += Bit ? X : Zero
#define FLV_BODY_MEAN             R->Summation.F64 += Bit ? (f64) X : 0.0

#define FLV_BODY_STDDEV                                                   \
    f64 D = Bit ? (f64) X - R->Shift : 0.0;                               \
    R->Summation.F64 += D;                                                \
    R->SummationSquared += D * D

#define FLV_BODY_MIN(Field) R->Minimum.Field = (Bit && X < R->Minimum.Field) ? X : R->Minimum.Field
#define FLV_BODY_MAX(Field) R->Maximum.Field = (Bit && X > R->Maximum.Field) ? X : R->Maximum.Field

#if FLV_KERNEL_SIMD
    // NOTE(nc): a reduction goes a word of the filter at a time. the samples
    //         : of the word that are in the reduction are its Live bits, so
    //         : the first, the last and the count of them are three bit
    //         : operations, and a word with no bit is skipped. the first
    //         : sample of the reduction is the first that is in it, as in
    //         : the scalar kernel.
    #define FLV_REDUCE_WIDE_KERNEL(Name, Type, Field, Kind, Body)            \
        INTERNAL                                                             \
        FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                 \
        {                                                                    \
            FLVReduction* R = &Lane->Reductions[FLVGetOperandIndex(Op.Dst)]; \
                                                                             \
            if (FLVTempIsMissing(Lane, Op.LHS)) {                            \
                FLVTempSetMissing(Lane, Op.Dst);                             \
                                                                             \
                return;                                                      \
            }                                                                \
                                                                             \
            u64 const* Mask = Lane->Mask ? Lane->Mask : Lane->MaskAll;       \
            Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);          \
            u64 Words = (Lane->ChunkCount + 63) >> 6;                        \
            u64 TailBits = Lane->ChunkCount & 63;                            \
                                                                             \
            for (u64 Word = 0; Word < Words; ++Word) {                       \
                Type const* X = A + (Word << 6);                             \
                u64 Live = Mask[Word];                                       \
                                                                             \
                if (TailBits && Word == Words - 1)                           \
                    Live &= (1ULL << TailBits) - 1;                          \
                                                                             \
                if (Live)                                                    \
                    Live = FLVLiveWord##Kind(X, Live);                       \
                                                                             \
                if (!Live)                                                   \
                    continue;                                                \
                                                                             \
                if (!R->Count) {                                             \
                    Type Head = X[Ctz64(Live)];                              \
                                                                             \
                    R->Head.Field = Head;                                    \
                    R->Minimum.Field = Head;                                 \
                    R->Maximum.Field = Head;                                 \
                    R->Shift = (f64) Head;                                   \
                }                                                            \
                                                                             \
                Body;                                                        \
                R->Tail.Field = X[63 - Clz64(Live)];                         \
                R->Count += PopCount64(Live);                                \
            }                                                                \
        }
    
    #define FLV_BODY_WIDE_NONE
    
    // NOTE(nc): the samples of a word that are not in the reduction are
    //         : read as the extreme so far, which changes nothing. four
    //         : groups are put together before the extreme takes them, so
    //         : one word is four steps of the chain and not 16.
    #define FLV_BODY_WIDE_EXTREME(Kind, Type, Field, WideM, ScalarM)    \
        FLV_WIDE_TYPE_##Kind Seed = FLV_WIDE_SET_##Kind(R->Field.Kind); \
        FLV_WIDE_TYPE_##Kind Wide = Seed;                               \
        Type Lanes[FLV_WIDE];                                           \
                                                                        \
        for (u64 Offset = 0; Offset < 64; Offset += 4 * FLV_WIDE) {     \
            FLV_WIDE_TYPE_##Kind V0 = FLV_WIDE_LIVE_##Kind(             \
                X, Live, Offset, Seed                                   \
            );                                                          \
            FLV_WIDE_TYPE_##Kind V1 = FLV_WIDE_LIVE_##Kind(             \
                X, Live, Offset + FLV_WIDE, Seed                        \
            );                                                          \
            FLV_WIDE_TYPE_##Kind V2 = FLV_WIDE_LIVE_##Kind(             \
                X, Live, Offset + 2 * FLV_WIDE, Seed                    \
            );                                                          \
            FLV_WIDE_TYPE_##Kind V3 = FLV_WIDE_LIVE_##Kind(             \
                X, Live, Offset + 3 * FLV_WIDE, Seed                    \
            );                                                          \
                                                                        \
            Wide = WideM(Wide, WideM(WideM(V0, V1), WideM(V2, V3)));    \
        }                                                               \
                                                                        \
        FLV_WIDE_STORE_##Kind(Lanes, Wide);                             \
        R->Field.Kind = ScalarM(                                        \
            ScalarM(Lanes[0], Lanes[1]),                                \
            ScalarM(Lanes[2], Lanes[3])                                 \
        )
    
    #define FLV_BODY_WIDE_MIN_F64 FLV_BODY_WIDE_EXTREME(F64, f64, Minimum, _mm256_min_pd, MIN)
    #define FLV_BODY_WIDE_MAX_F64 FLV_BODY_WIDE_EXTREME(F64, f64, Maximum, _mm256_max_pd, MAX)
    #define FLV_BODY_WIDE_MIN_I64 FLV_BODY_WIDE_EXTREME(I64, i64, Minimum, FLVWideMinI64, MIN)
    #define FLV_BODY_WIDE_MAX_I64 FLV_BODY_WIDE_EXTREME(I64, i64, Maximum, FLVWideMaxI64, MAX)
    
    #define FLV_BODY_WIDE_SUM_I64                               \
        v256 Zero = _mm256_setzero_si256();                     \
        v256 Wide = Zero;                                       \
        i64 Lanes[FLV_WIDE];                                    \
                                                                \
        for (u64 Offset = 0; Offset < 64; Offset += FLV_WIDE) { \
            Wide = _mm256_add_epi64(                            \
                Wide,                                           \
                FLV_WIDE_LIVE_I64(X, Live, Offset, Zero)        \
            );                                                  \
        }                                                       \
                                                                \
        _mm256_storeu_si256((v256*) Lanes, Wide);               \
        R->Summation.I64 += Lanes[0] + Lanes[1] + Lanes[2] + Lanes[3]
    
    // NOTE(nc): a sum of f64 is not wide: four sums at a time add the
    //         : samples in another order, and the last digits of a mean, a
    //         : sum and a stddev would not be the ones of the scalar build
    FLV_REDUCE_KERNEL(MEAN_I64,     i64, I64, FLV_VALID_I64, FLV_BODY_MEAN)
    FLV_REDUCE_KERNEL(MEAN_F64,     f64, F64, FLV_VALID_F64, FLV_BODY_MEAN)
    FLV_REDUCE_KERNEL(STDDEV_I64,   i64, I64, FLV_VALID_I64, FLV_BODY_STDDEV)
    FLV_REDUCE_KERNEL(STDDEV_F64,   f64, F64, FLV_VALID_F64, FLV_BODY_STDDEV)
    FLV_REDUCE_KERNEL(SUM_F64,      f64, F64, FLV_VALID_F64, FLV_BODY_SUM(F64, 0.0))
    FLV_REDUCE_WIDE_KERNEL(MIN_I64,          i64, I64, I64, FLV_BODY_WIDE_MIN_I64)
    FLV_REDUCE_WIDE_KERNEL(MIN_F64,          f64, F64, F64, FLV_BODY_WIDE_MIN_F64)
    FLV_REDUCE_WIDE_KERNEL(MAX_I64,          i64, I64, I64, FLV_BODY_WIDE_MAX_I64)
    FLV_REDUCE_WIDE_KERNEL(MAX_F64,          f64, F64, F64, FLV_BODY_WIDE_MAX_F64)
    FLV_REDUCE_WIDE_KERNEL(SUM_I64,          i64, I64, I64, FLV_BODY_WIDE_SUM_I64)
    FLV_REDUCE_WIDE_KERNEL(FIRST_I64,        i64, I64, I64, FLV_BODY_WIDE_NONE)
    FLV_REDUCE_WIDE_KERNEL(FIRST_F64,        f64, F64, F64, FLV_BODY_WIDE_NONE)
    FLV_REDUCE_WIDE_KERNEL(LAST_I64,         i64, I64, I64, FLV_BODY_WIDE_NONE)
    FLV_REDUCE_WIDE_KERNEL(LAST_F64,         f64, F64, F64, FLV_BODY_WIDE_NONE)
    FLV_REDUCE_WIDE_KERNEL(REDUCE_COUNT,     i64, I64, I64, FLV_BODY_WIDE_NONE)
    FLV_REDUCE_WIDE_KERNEL(REDUCE_COUNT_F64, f64, F64, F64, FLV_BODY_WIDE_NONE)
    
    #undef FLV_BODY_WIDE_SUM_I64
    #undef FLV_BODY_WIDE_MAX_I64
    #undef FLV_BODY_WIDE_MIN_I64
    #undef FLV_BODY_WIDE_MAX_F64
    #undef FLV_BODY_WIDE_MIN_F64
    #undef FLV_BODY_WIDE_EXTREME
    #undef FLV_BODY_WIDE_NONE
    #undef FLV_REDUCE_WIDE_KERNEL
    
    #undef FLV_WIDE_LIVE_I64
    #undef FLV_WIDE_LIVE_F64
    #undef FLV_WIDE_CMP_NE_I64
    #undef FLV_WIDE_CMP_EQ_I64
    #undef FLV_WIDE_CMP_GE_I64
    #undef FLV_WIDE_CMP_GT_I64
    #undef FLV_WIDE_CMP_LE_I64
    #undef FLV_WIDE_CMP_LT_I64
    #undef FLV_WIDE_CMP_NE_F64
    #undef FLV_WIDE_CMP_EQ_F64
    #undef FLV_WIDE_CMP_GE_F64
    #undef FLV_WIDE_CMP_GT_F64
    #undef FLV_WIDE_CMP_LE_F64
    #undef FLV_WIDE_CMP_LT_F64
    #undef FLV_WIDE_SUB_I64
    #undef FLV_WIDE_ADD_I64
    #undef FLV_WIDE_MOD_F64
    #undef FLV_WIDE_DIV_F64
    #undef FLV_WIDE_MUL_F64
    #undef FLV_WIDE_SUB_F64
    #undef FLV_WIDE_ADD_F64
    #undef FLV_WIDE_BITS_I64
    #undef FLV_WIDE_BITS_F64
    #undef FLV_WIDE_SET_I64
    #undef FLV_WIDE_SET_F64
    #undef FLV_WIDE_STORE_I64
    #undef FLV_WIDE_STORE_F64
    #undef FLV_WIDE_LOAD_I64
    #undef FLV_WIDE_LOAD_F64
    #undef FLV_WIDE_TYPE_I64
    #undef FLV_WIDE_TYPE_F64
#else
    FLV_REDUCE_KERNEL(MEAN_I64,     i64, I64, FLV_VALID_I64, FLV_BODY_MEAN)
    FLV_REDUCE_KERNEL(MEAN_F64,     f64, F64, FLV_VALID_F64, FLV_BODY_MEAN)
    FLV_REDUCE_KERNEL(STDDEV_I64,   i64, I64, FLV_VALID_I64, FLV_BODY_STDDEV)
    FLV_REDUCE_KERNEL(STDDEV_F64,   f64, F64, FLV_VALID_F64, FLV_BODY_STDDEV)
    FLV_REDUCE_KERNEL(MIN_I64,      i64, I64, FLV_VALID_I64, FLV_BODY_MIN(I64))
    FLV_REDUCE_KERNEL(MIN_F64,      f64, F64, FLV_VALID_F64, FLV_BODY_MIN(F64))
    FLV_REDUCE_KERNEL(MAX_I64,      i64, I64, FLV_VALID_I64, FLV_BODY_MAX(I64))
    FLV_REDUCE_KERNEL(MAX_F64,      f64, F64, FLV_VALID_F64, FLV_BODY_MAX(F64))
    FLV_REDUCE_KERNEL(SUM_I64,      i64, I64, FLV_VALID_I64, FLV_BODY_SUM(I64, 0))
    FLV_REDUCE_KERNEL(SUM_F64,      f64, F64, FLV_VALID_F64, FLV_BODY_SUM(F64, 0.0))
    FLV_REDUCE_KERNEL(FIRST_I64,    i64, I64, FLV_VALID_I64, FLV_BODY_NONE)
    FLV_REDUCE_KERNEL(FIRST_F64,    f64, F64, FLV_VALID_F64, FLV_BODY_NONE)
    FLV_REDUCE_KERNEL(LAST_I64,     i64, I64, FLV_VALID_I64, FLV_BODY_NONE)
    FLV_REDUCE_KERNEL(LAST_F64,     f64, F64, FLV_VALID_F64, FLV_BODY_NONE)
    FLV_REDUCE_KERNEL(REDUCE_COUNT, i64, I64, FLV_VALID_I64, FLV_BODY_NONE)
    FLV_REDUCE_KERNEL(REDUCE_COUNT_F64, f64, F64, FLV_VALID_F64, FLV_BODY_NONE)
#endif

#undef FLV_BODY_MAX
#undef FLV_BODY_MIN
#undef FLV_BODY_STDDEV
#undef FLV_BODY_MEAN
#undef FLV_BODY_SUM
#undef FLV_BODY_NONE
#undef FLV_REDUCE_KERNEL

#define FLV_RING(W, Index) ((Index) & ((W)->Capacity - 1))
 
#define FLV_WINDOW_HEAD(Type)                                             \
    FLVWindow* W = &Lane->Windows[FLVGetOperandIndex(Op.Dst)];            \
                                                                          \
    if (FLVTempIsMissing(Lane, Op.LHS)) {                                 \
        FLVTempSetMissing(Lane, Op.Dst);                                  \
                                                                          \
        return;                                                           \
    }                                                                     \
                                                                          \
    u64 const* Mask = Lane->Mask ? Lane->Mask : Lane->MaskAll;            \
    Type const* A = (Type const*) FLVTempPtr(Lane, Op.LHS);               \
    u64 const*  T = Lane->ChunkTime;                                      \
    u64 Count = Lane->ChunkCount
 
// NOTE(nc): a sample that the filter takes out, or that has no value, is
//         : skipped, and the next sample is taken from the last one that
//         : was in. a gap does not reset the prev: an instance is a mask
//         : over samples that alternate, so a reset would lose them all.
#define FLV_DIFF_KERNEL(Name, Type, Field, Hole, Valid)                   \
    INTERNAL                                                              \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                  \
    {                                                                     \
        FLV_WINDOW_HEAD(Type);                                            \
        Type* Dst = (Type*) FLVTempPtr(Lane, Op.Dst);                     \
        Type  Prev = W->Prev.Field;                                       \
        b32   HasPrev = W->HasPrev;                                       \
                                                                          \
        for (u64 Index = 0; Index < Count; ++Index) {                     \
            Type X = A[Index];                                            \
                                                                          \
            if (!FLVMaskBit(Mask, Index) || !Valid(X)) {                  \
                Dst[Index] = Hole;                                        \
                continue;                                                 \
            }                                                             \
                                                                          \
            Dst[Index] = HasPrev ? X - Prev : Hole;                       \
            Prev = X;                                                     \
            HasPrev = TRUE;                                               \
        }                                                                 \
                                                                          \
        W->Prev.Field = Prev;                                             \
        W->HasPrev = HasPrev;                                             \
    }
 
FLV_DIFF_KERNEL(DIFF_I64, i64, I64, FLV_I64_HOLE, FLV_VALID_I64)
FLV_DIFF_KERNEL(DIFF_F64, f64, F64, NC_NAN, FLV_VALID_F64)
 
#undef FLV_DIFF_KERNEL

// NOTE(nc): two samples at one time have no rate between them. the later
//         : one is the prev, so the next rate is from the newest value.
#define FLV_RATE_KERNEL(Name, Type, Field, Valid)                         \
    INTERNAL                                                              \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                  \
    {                                                                     \
        FLV_WINDOW_HEAD(Type);                                            \
        f64*  Dst = (f64*) FLVTempPtr(Lane, Op.Dst);                      \
        Type  Prev = W->Prev.Field;                                       \
        u64   PrevTime = W->PrevTimeUSecs;                                \
        b32   HasPrev = W->HasPrev;                                       \
                                                                          \
        for (u64 Index = 0; Index < Count; ++Index) {                     \
            Type X = A[Index];                                            \
                                                                          \
            if (!FLVMaskBit(Mask, Index) || !Valid(X)) {                  \
                Dst[Index] = NC_NAN;                                      \
                continue;                                                 \
            }                                                             \
                                                                          \
            f64 Step = (f64) (i64) (T[Index] - PrevTime);                 \
                                                                          \
            Dst[Index] = (HasPrev && Step > 0.0)                          \
                ? ((f64) X - (f64) Prev) * SECONDS(1.0) / Step            \
                : NC_NAN;                                                 \
            Prev = X;                                                     \
            PrevTime = T[Index];                                          \
            HasPrev = TRUE;                                               \
        }                                                                 \
                                                                          \
        W->Prev.Field = Prev;                                             \
        W->PrevTimeUSecs = PrevTime;                                      \
        W->HasPrev = HasPrev;                                             \
    }
 
FLV_RATE_KERNEL(RATE_I64, i64, I64, FLV_VALID_I64)
FLV_RATE_KERNEL(RATE_F64, f64, F64, FLV_VALID_F64)

#undef FLV_RATE_KERNEL
 
#define FLV_EVICT_FRONT(Body)                                                \
    while (W->Count && W->Times[FLV_RING(W, W->Head)] + Width <= T[Index]) { \
        Body;                                                                \
        ++W->Head;                                                           \
        --W->Count;                                                          \
    }
 
// NOTE(nc): a sample with no value is skipped, as in a diff and a rate:
//         : it is not put in the window and it has no value out. the
//         : first sample of a rate is one, so a window that took it in
//         : would have no value after it. the sum of a mean is a
//         : running one, so an infinity is kept out of it as well.
#define FLV_ROLLING_MEAN_KERNEL(Name, Type, Field, Hole, Valid)             \
    INTERNAL                                                                \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                    \
    {                                                                       \
        FLV_WINDOW_HEAD(Type);                                              \
        Type* Dst = (Type*) FLVTempPtr(Lane, Op.Dst);                       \
        u64  Width = FLVScalarTimeUSecs(Lane, Op.RHS);                      \
                                                                            \
        for (u64 Index = 0; Index < Count; ++Index) {                       \
            Type X = A[Index];                                              \
                                                                            \
            if (!FLVMaskBit(Mask, Index) || !Valid(X)) {                    \
                Dst[Index] = Hole;                                          \
                continue;                                                   \
            }                                                               \
                                                                            \
            FLV_EVICT_FRONT(                                                \
                W->Summation.Field -= W->Values[FLV_RING(W, W->Head)].Field \
            );                                                              \
                                                                            \
            if (!W->Count)                                                  \
                W->Summation.Field = 0;                                     \
                                                                            \
            u64 Back = FLV_RING(W, W->Head + W->Count);                     \
                                                                            \
            W->Times[Back] = T[Index];                                      \
            W->Values[Back].Field = X;                                      \
            W->Summation.Field += X;                                        \
            ++W->Count;                                                     \
            Dst[Index] = W->Summation.Field / (Type) W->Count;              \
        }                                                                   \
    }

#define FLV_ROLLING_EXTREME_KERNEL(Name, Type, Field, Hole, Valid, PopSym) \
    INTERNAL                                                              \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                                  \
    {                                                                     \
        FLV_WINDOW_HEAD(Type);                                            \
        Type* Dst = (Type*) FLVTempPtr(Lane, Op.Dst);                     \
        u64  Width = FLVScalarTimeUSecs(Lane, Op.RHS);                    \
                                                                          \
        for (u64 Index = 0; Index < Count; ++Index) {                     \
            Type X = A[Index];                                            \
                                                                          \
            if (!FLVMaskBit(Mask, Index) || !Valid(X)) {                  \
                Dst[Index] = Hole;                                        \
                continue;                                                 \
            }                                                             \
                                                                          \
            FLV_EVICT_FRONT({});                                          \
                                                                          \
            while (                                                       \
                W->Count &&                                               \
                W->Values[FLV_RING(W, W->Head + W->Count - 1)].Field      \
                    PopSym X                                              \
            ) {                                                           \
                --W->Count;                                               \
            }                                                             \
                                                                          \
            u64 Back = FLV_RING(W, W->Head + W->Count);                   \
                                                                          \
            W->Times[Back] = T[Index];                                    \
            W->Values[Back].Field = X;                                    \
            ++W->Count;                                                   \
            Dst[Index] = W->Values[FLV_RING(W, W->Head)].Field;           \
        }                                                                 \
    }

FLV_ROLLING_MEAN_KERNEL(ROLLING_MEAN_I64, i64, I64, FLV_I64_HOLE, FLV_VALID_I64)
FLV_ROLLING_MEAN_KERNEL(ROLLING_MEAN_F64, f64, F64, NC_NAN, FLV_FINITE_F64)
FLV_ROLLING_EXTREME_KERNEL(ROLLING_MIN_I64, i64, I64, FLV_I64_HOLE, FLV_VALID_I64, >=)
FLV_ROLLING_EXTREME_KERNEL(ROLLING_MIN_F64, f64, F64, NC_NAN, FLV_VALID_F64, >=)
FLV_ROLLING_EXTREME_KERNEL(ROLLING_MAX_I64, i64, I64, FLV_I64_HOLE, FLV_VALID_I64, <=)
FLV_ROLLING_EXTREME_KERNEL(ROLLING_MAX_F64, f64, F64, NC_NAN, FLV_VALID_F64, <=)

#undef FLV_ROLLING_EXTREME_KERNEL
#undef FLV_ROLLING_MEAN_KERNEL
#undef FLV_EVICT_FRONT
#undef FLV_WINDOW_HEAD
#undef FLV_RING
#undef FLV_FINITE_F64
#undef FLV_VALID_F64
#undef FLV_VALID_I64

#define FLV_S_HEAD                                                              \
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {     \
        FLVTempSetMissing(Lane, Op.Dst);                                        \
                                                                                \
        return;                                                                 \
    }
 
#define FLV_S_BINARY(Name, Type, DstType, ScalarFunc, Expr)   \
    INTERNAL                                                  \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                      \
    {                                                         \
        FLV_S_HEAD                                            \
                                                              \
        Type A = ScalarFunc(Lane, Op.LHS);                    \
        Type B = ScalarFunc(Lane, Op.RHS);                    \
        DstType* Dst = (DstType*) FLVTempPtr(Lane, Op.Dst);   \
                                                              \
        *Dst = (Expr);                                        \
    }
 
#define FLV_S_UNARY(Name, Type, DstType, ScalarFunc, Expr)    \
    INTERNAL                                                  \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                      \
    {                                                         \
        FLV_S_HEAD                                            \
                                                              \
        Type A = ScalarFunc(Lane, Op.LHS);                    \
        DstType* Dst = (DstType*) FLVTempPtr(Lane, Op.Dst);   \
                                                              \
        *Dst = (Expr);                                        \
    }
 
#define FLV_S_DIVIDE(Name, Expr)                              \
    INTERNAL                                                  \
    FLV_KERNEL_FUNC_SIG(FLVKernel##Name)                      \
    {                                                         \
        FLV_S_HEAD                                            \
                                                              \
        i64 A = FLVScalarI64(Lane, Op.LHS);                   \
        i64 B = FLVScalarI64(Lane, Op.RHS);                   \
        i64* Dst = (i64*) FLVTempPtr(Lane, Op.Dst);           \
                                                              \
        if (!B) {                                             \
            FLVTempSetMissing(Lane, Op.Dst);                  \
                                                              \
            return;                                           \
        }                                                     \
                                                              \
        *Dst = (Expr);                                        \
    }
 
FLV_S_BINARY(S_ADD_I64, i64, i64, FLVScalarI64, A + B)
FLV_S_BINARY(S_ADD_F64, f64, f64, FLVScalarF64, A + B)
FLV_S_BINARY(S_SUB_I64, i64, i64, FLVScalarI64, A - B)
FLV_S_BINARY(S_SUB_F64, f64, f64, FLVScalarF64, A - B)
FLV_S_BINARY(S_MUL_I64, i64, i64, FLVScalarI64, A * B)
FLV_S_BINARY(S_MUL_F64, f64, f64, FLVScalarF64, A * B)
FLV_S_DIVIDE(S_DIV_I64, A / B)
FLV_S_BINARY(S_DIV_F64, f64, f64, FLVScalarF64, A / B)
FLV_S_DIVIDE(S_MOD_I64, A % B)
FLV_S_BINARY(S_MOD_F64, f64, f64, FLVScalarF64, FMod(A, B))
FLV_S_UNARY(S_NEG_I64, i64, i64, FLVScalarI64, -A)
FLV_S_UNARY(S_NEG_F64, f64, f64, FLVScalarF64, -A)
FLV_S_UNARY(S_ABS_I64, i64, i64, FLVScalarI64, Abs64(A))
FLV_S_UNARY(S_ABS_F64, f64, f64, FLVScalarF64, AbsF64(A))
FLV_S_UNARY(S_SQRT_F64, f64, f64, FLVScalarF64, SqrtF64(A))
FLV_S_BINARY(S_CMP_LT_I64, i64, i64, FLVScalarI64, A < B)
FLV_S_BINARY(S_CMP_LT_F64, f64, i64, FLVScalarF64, A < B)
FLV_S_BINARY(S_CMP_LE_I64, i64, i64, FLVScalarI64, A <= B)
FLV_S_BINARY(S_CMP_LE_F64, f64, i64, FLVScalarF64, A <= B)
FLV_S_BINARY(S_CMP_GT_I64, i64, i64, FLVScalarI64, A > B)
FLV_S_BINARY(S_CMP_GT_F64, f64, i64, FLVScalarF64, A > B)
FLV_S_BINARY(S_CMP_GE_I64, i64, i64, FLVScalarI64, A >= B)
FLV_S_BINARY(S_CMP_GE_F64, f64, i64, FLVScalarF64, A >= B)
FLV_S_BINARY(S_CMP_EQ_I64, i64, i64, FLVScalarI64, A == B)
FLV_S_BINARY(S_CMP_EQ_F64, f64, i64, FLVScalarF64, A == B)
FLV_S_BINARY(S_CMP_NE_I64, i64, i64, FLVScalarI64, A != B)
FLV_S_BINARY(S_CMP_NE_F64, f64, i64, FLVScalarF64, A != B)
FLV_S_BINARY(S_AND, i64, i64, FLVScalarI64, (A != 0) && (B != 0))
FLV_S_BINARY(S_OR,  i64, i64, FLVScalarI64, (A != 0) || (B != 0))
FLV_S_UNARY(S_NOT, i64, i64, FLVScalarI64, A == 0)
FLV_S_UNARY(S_CONVERT_I64_F64, i64, f64, FLVScalarI64, (f64) A)
 
#undef FLV_S_DIVIDE
#undef FLV_S_UNARY
#undef FLV_S_BINARY
#undef FLV_S_HEAD

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_DURATION)
{
    if (FLVTempIsMissing(Lane, Op.LHS)) {
        FLVTempSetMissing(Lane, Op.Dst);
 
        return;
    }
 
    FLVSelection const* Sel = FLVSelectionPtr(Lane, Op.LHS);
    i64* Dst = (i64*) FLVTempPtr(Lane, Op.Dst);
    u64 Total = 0;
 
    for (u64 Index = 0; Index < Sel->Count; ++Index)
        Total += Sel->V[Index].Max - Sel->V[Index].Min;
 
    *Dst = (i64) Total;
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_FROM_MASK)
{
    FLVSelectionBuild* Build = &Lane->SelectionBuilds[
        FLVGetOperandIndex(Op.Dst)
    ];

    if (FLVTempIsMissing(Lane, Op.LHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    u64 const* Mask = (u64 const*) FLVTempPtr(Lane, Op.LHS);

    for (u64 Index = 0; Index < Lane->ChunkCount; ++Index) {
        b32 Bit = FLVMaskBit(Mask, Index);

        if (Bit) {
            if (!Build->IsOpen) {
                Build->IsOpen = TRUE;
                Build->OpenStartUSecs = Lane->ChunkTime[Index];
            }

            Build->OpenEndUSecs = Lane->ChunkTime[Index];
        } else if (Build->IsOpen) {
            FLVSelectionBuildPush(
                Lane, 
                Build, 
                Rng(Build->OpenStartUSecs, Build->OpenEndUSecs)
            );
            Build->IsOpen = FALSE;
        }
    }
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_FROM_TIME_RANGE)
{
    FLVSelection* Sel = FLVSelectionPtr(Lane, Op.Dst);
    u64 Min = FLVScalarTimeUSecs(Lane, Op.LHS);
    u64 EndUSecs = FLVScalarTimeUSecs(Lane, Op.RHS);

    if (EndUSecs > Lane->FlightEndUSecs)
        EndUSecs = Lane->FlightEndUSecs;

    *Sel = FLVSelectionAlloc(Lane, 1);

    if (Min <= EndUSecs) {
        Sel->V[0].Min = Min;
        Sel->V[0].Max = EndUSecs;
        Sel->Count = 1;
    }
}

INTERNAL u64
FLVModeRanges(FLVLane* Lane, u32 ModeNumber, OUT r1u64* Result)
{
    u64 ModesCount = 0;
    FLIMode* Modes = FLITableFromName(
        Lane->FLI, 
        Modes, 
        MODES, 
        &ModesCount
    );
    u64 Count = 0;
    r1u64 Last = {};

    for (u64 Index = 0; Index < ModesCount; ++Index) {
        if (Modes[Index].Mode != ModeNumber)
            continue;

        u64 EndUSecs = (Index + 1 < ModesCount)
            ? Modes[Index + 1].TimeUSecs
            : Lane->FlightEndUSecs;

        if (Count && Last.Max == Modes[Index].TimeUSecs) {
            Last.Max = EndUSecs;
        } else {
            Last.Min = Modes[Index].TimeUSecs;
            Last.Max = EndUSecs;
            ++Count;
        }

        if (Result)
            Result[Count - 1] = Last;
    }

    return Count;
}

INTERNAL u64
FLVEventRanges(FLVLane* Lane, u8 ID, OUT r1u64* Result)
{
    u64 EventsCount = 0;
    FLIEvt* Events = FLITableFromName(
        Lane->FLI, 
        Events, 
        EVENTS, 
        &EventsCount
    );
    u64 Count = 0;

    for (u64 Index = 0; Index < EventsCount; ++Index) {
        if (Events[Index].ID != ID)
            continue;

        if (Result) {
            Result[Count] = Rng(
                Events[Index].TimeUSecs, 
                Events[Index].TimeUSecs
            );
         }

        ++Count;
    }

    return Count;
}

INTERNAL u64
FLVArmedRanges(FLVLane* Lane, OUT r1u64* Result)
{
    u64 EventsCount = 0;
    FLIEvt* Events = FLITableFromName(
        Lane->FLI, 
        Events, 
        EVENTS, 
        &EventsCount
    );
    u64 Count = 0;
    b32 IsArmed = FALSE;
    u64 ArmedUSecs = 0;

    for (u64 Index = 0; Index < EventsCount; ++Index) {
        if (Events[Index].ID == FLV_EVENT_ID_ARMED && !IsArmed) {
            IsArmed = TRUE;
            ArmedUSecs = Events[Index].TimeUSecs;
        } else if (
            Events[Index].ID == FLV_EVENT_ID_DISARMED && 
            IsArmed
        ) {
            if (Result)
                Result[Count] = Rng(ArmedUSecs, Events[Index].TimeUSecs);

            ++Count;
            IsArmed = FALSE;
        }
    }

    if (IsArmed) {
        if (Result)
            Result[Count] = Rng(ArmedUSecs, Lane->FlightEndUSecs);

        ++Count;
    }

    return Count;
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_MODE)
{
    FLVSelection* Sel = FLVSelectionPtr(Lane, Op.Dst);
    u32 Slot = FLVGetOperandIndex(Op.LHS);
    FLVBoundSlot const* Bound = &Lane->Slots[Slot];

    ASSERT(FLVGetOperandKind(Op.LHS) == FLV_OPERAND_KIND_SLOT);

    if (Bound->IsMissing || Bound->ModeNumber == FLV_MODE_NUMBER_NONE) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    u64 Count = FLVModeRanges(Lane, Bound->ModeNumber, NULL);

    *Sel = FLVSelectionAlloc(Lane, Count);
    Sel->Count = FLVModeRanges(Lane, Bound->ModeNumber, Sel->V);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_EVENT)
{
    FLVSelection* Sel = FLVSelectionPtr(Lane, Op.Dst);
    u8 ID = (u8) FLVScalarI64(Lane, Op.LHS);
    u64 Count = FLVEventRanges(Lane, ID, NULL);

    *Sel = FLVSelectionAlloc(Lane, Count);
    Sel->Count = FLVEventRanges(Lane, ID, Sel->V);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_ARMED)
{
    FLVSelection* Sel = FLVSelectionPtr(Lane, Op.Dst);
    u64 Count = FLVArmedRanges(Lane, NULL);

    *Sel = FLVSelectionAlloc(Lane, Count);
    Sel->Count = FLVArmedRanges(Lane, Sel->V);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_AND)
{
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    FLVSelection const* A = FLVSelectionPtr(Lane, Op.LHS);
    FLVSelection const* B = FLVSelectionPtr(Lane, Op.RHS);
    FLVSelection* Dst = FLVSelectionPtr(Lane, Op.Dst);
    u64 IndexA = 0;
    u64 IndexB = 0;

    *Dst = FLVSelectionAlloc(Lane, A->Count + B->Count);

    while (IndexA < A->Count && IndexB < B->Count) {
        r1u64 RangeA = A->V[IndexA];
        r1u64 RangeB = B->V[IndexB];
        u64 Min = MAX(RangeA.Min, RangeB.Min);
        u64 Max = MIN(RangeA.Max, RangeB.Max);

        Dst->V[Dst->Count] = Rng(Min, Max);
        Dst->Count += (Min <= Max);

        if (RangeA.Max < RangeB.Max)
            ++IndexA;
        else
            ++IndexB;
    }
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_OR)
{
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    FLVSelection const* A = FLVSelectionPtr(Lane, Op.LHS);
    FLVSelection const* B = FLVSelectionPtr(Lane, Op.RHS);
    FLVSelection* Dst = FLVSelectionPtr(Lane, Op.Dst);
    u64 IndexA = 0;
    u64 IndexB = 0;

    *Dst = FLVSelectionAlloc(Lane, A->Count + B->Count);

    while (IndexA < A->Count || IndexB < B->Count) {
        r1u64 Next = {};
        b32 TakeA = (
            IndexB >= B->Count ||
            (
               IndexA < A->Count && 
               A->V[IndexA].Min <= B->V[IndexB].Min
            )
        );

        if (TakeA)
            Next = A->V[IndexA++];
        else
            Next = B->V[IndexB++];

        if (
            Dst->Count && 
            Next.Min <= Dst->V[Dst->Count - 1].Max
        ) {
            Dst->V[Dst->Count - 1].Max = MAX(
                Dst->V[Dst->Count - 1].Max,
                Next.Max
            );
        } else {
            Dst->V[Dst->Count] = Next;
            ++Dst->Count;
        }
    }
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_NOT)
{
    if (FLVTempIsMissing(Lane, Op.LHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    FLVSelection const* A = FLVSelectionPtr(Lane, Op.LHS);
    FLVSelection* Dst = FLVSelectionPtr(Lane, Op.Dst);
    u64 Cursor = 0;

    *Dst = FLVSelectionAlloc(Lane, A->Count + 1);

    for (u64 Index = 0; Index < A->Count; ++Index) {
        if (A->V[Index].Min > Cursor) {
            Dst->V[Dst->Count].Min = Cursor;
            Dst->V[Dst->Count].Max = A->V[Index].Min - 1;
            ++Dst->Count;
        }

        Cursor = A->V[Index].Max + 1;
    }

    if (Cursor <= Lane->FlightEndUSecs) {
        Dst->V[Dst->Count].Min = Cursor;
        Dst->V[Dst->Count].Max = Lane->FlightEndUSecs;
        ++Dst->Count;
    }
}

// NOTE(nc): the ranges of a selection with each gap of Gap or less
//         : closed, so the ranges at its two sides are one range
INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_CLOSE)
{
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    FLVSelection const* A = FLVSelectionPtr(Lane, Op.LHS);
    FLVSelection* Dst = FLVSelectionPtr(Lane, Op.Dst);
    u64 Gap = FLVScalarTimeUSecs(Lane, Op.RHS);

    *Dst = FLVSelectionAlloc(Lane, A->Count);

    for (u64 Index = 0; Index < A->Count; ++Index) {
        r1u64 Next = A->V[Index];

        if (
            Dst->Count &&
            Next.Min <= Dst->V[Dst->Count - 1].Max + Gap
        ) {
            Dst->V[Dst->Count - 1].Max = MAX(
                Dst->V[Dst->Count - 1].Max,
                Next.Max
            );
        } else {
            Dst->V[Dst->Count] = Next;
            ++Dst->Count;
        }
    }
}

// NOTE(nc): the ranges of a selection that are as long as Width or
//         : longer. a range of one sample has no length.
INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_LONGER)
{
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    FLVSelection const* A = FLVSelectionPtr(Lane, Op.LHS);
    FLVSelection* Dst = FLVSelectionPtr(Lane, Op.Dst);
    u64 Width = FLVScalarTimeUSecs(Lane, Op.RHS);

    *Dst = FLVSelectionAlloc(Lane, A->Count);

    for (u64 Index = 0; Index < A->Count; ++Index) {
        Dst->V[Dst->Count] = A->V[Index];
        Dst->Count += (A->V[Index].Max - A->V[Index].Min >= Width);
    }
}

// NOTE(nc): the ranges of A that touch a range of B. two ranges touch
//         : if they have a time in common, or if one starts at the
//         : microsecond after the end of the other, as a range and the
//         : ranges of its ! do. BEFORE keeps a range that starts before
//         : the range of B and runs up to it or into it. AFTER keeps a
//         : range that starts in the range of B, or at its end, and runs
//         : on after it.
INTERNAL void
FLVSelectionTouch(FLVLane* Lane, FLVOp Op, FLVTouchKind Kind)
{
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    FLVSelection const* A = FLVSelectionPtr(Lane, Op.LHS);
    FLVSelection const* B = FLVSelectionPtr(Lane, Op.RHS);
    FLVSelection* Dst = FLVSelectionPtr(Lane, Op.Dst);
    u64 HeadB = 0;

    *Dst = FLVSelectionAlloc(Lane, A->Count);

    for (u64 IndexA = 0; IndexA < A->Count; ++IndexA) {
        r1u64 RangeA = A->V[IndexA];
        b32 IsKept = FALSE;

        while (HeadB < B->Count && B->V[HeadB].Max + 1 < RangeA.Min)
            ++HeadB;

        for (
            u64 IndexB = HeadB;
            !IsKept && IndexB < B->Count && B->V[IndexB].Min <= RangeA.Max + 1;
            ++IndexB
        ) {
            r1u64 RangeB = B->V[IndexB];

            switch (Kind) {
                default: {
                    IsKept = TRUE;
                } break;

                case FLV_TOUCH_KIND_BEFORE: {
                    IsKept = (
                        RangeA.Min < RangeB.Min &&
                        RangeA.Max <= RangeB.Max
                    );
                } break;

                case FLV_TOUCH_KIND_AFTER: {
                    IsKept = (
                        RangeA.Min >= RangeB.Min &&
                        RangeA.Max > RangeB.Max
                    );
                } break;
            }
        }

        Dst->V[Dst->Count] = RangeA;
        Dst->Count += IsKept;
    }
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_TOUCH)
{
    FLVSelectionTouch(Lane, Op, FLV_TOUCH_KIND_ANY);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_BEFORE)
{
    FLVSelectionTouch(Lane, Op, FLV_TOUCH_KIND_BEFORE);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_AFTER)
{
    FLVSelectionTouch(Lane, Op, FLV_TOUCH_KIND_AFTER);
}

// NOTE(nc): one range of a selection, as a selection. it has no range if
//         : the selection has no range at that place.
INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_PICK)
{
    if (FLVTempIsMissing(Lane, Op.LHS) || FLVTempIsMissing(Lane, Op.RHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    FLVSelection const* A = FLVSelectionPtr(Lane, Op.LHS);
    FLVSelection* Dst = FLVSelectionPtr(Lane, Op.Dst);
    u64 Index = (u64) FLVScalarI64(Lane, Op.RHS);

    *Dst = FLVSelectionAlloc(Lane, 1);

    if (Index < A->Count) {
        Dst->V[0] = A->V[Index];
        Dst->Count = 1;
    }
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_COUNT)
{
    if (FLVTempIsMissing(Lane, Op.LHS)) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    FLVSelection const* Sel = FLVSelectionPtr(Lane, Op.LHS);
    i64* Dst = (i64*) FLVTempPtr(Lane, Op.Dst);

    *Dst = (i64) Sel->Count;
}

// NOTE(nc): the time of the start of the first range, and of the end of
//         : the last one. a selection with no range has no such time.
INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_START)
{
    FLVSelection const* Sel = FLVSelectionPtr(Lane, Op.LHS);
    i64* Dst = (i64*) FLVTempPtr(Lane, Op.Dst);

    if (FLVTempIsMissing(Lane, Op.LHS) || !Sel->Count) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    *Dst = (i64) Sel->V[0].Min;
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelSEL_END)
{
    FLVSelection const* Sel = FLVSelectionPtr(Lane, Op.LHS);
    i64* Dst = (i64*) FLVTempPtr(Lane, Op.Dst);

    if (FLVTempIsMissing(Lane, Op.LHS) || !Sel->Count) {
        FLVTempSetMissing(Lane, Op.Dst);

        return;
    }

    *Dst = (i64) Sel->V[Sel->Count - 1].Max;
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_CURVE)
{
    u32 EmitIndex = FLVGetOperandIndex(Op.RHS);
    FLVRenderItem* Item = &Lane->Items[EmitIndex];
    FLVRenderCurve* Curve = &Item->Curve;
    FLVEmitBuild* Build = &Lane->EmitBuilds[EmitIndex];
    u64 Count = Lane->ChunkCount;
    u64 Position = Lane->ChunkPosition;

    if (Item->IsMissing)
        return;

    if (Build->SourceSlot != U32_MAX) {
        if (Curve->Mask) {
            if (Lane->Mask)
                FLVMaskOrBits(Curve->Mask, Position, Lane->Mask, Count);
            else
                FLVMaskSetRange(Curve->Mask, Position, Count);
        }

        return;
    }

    if (FLVTempIsMissing(Lane, Op.LHS)) {
        Item->IsMissing = TRUE;

        return;
    }

    f64 const* Value = (f64 const*) FLVTempPtr(Lane, Op.LHS);

    for (u64 Index = 0; Index < Count; ++Index) {
        f64 V = Value[Index];

        if (Lane->Mask && !FLVMaskBit(Lane->Mask, Index))
            continue;

        f32 Stored = (f32) V;

        if (Stored * 0.0f != 0.0f)
            continue;

        FLVMaskSetBit(Curve->Mask, Position + Index);
        FLVEmitBuildPush(Lane, Build, &Stored, sizeof(Stored));
    }
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_FLAG)
{
    u32 EmitIndex = FLVGetOperandIndex(Op.RHS);
    FLVEmit const* Emit = &Lane->Program->Emits[EmitIndex];
    FLVRenderItem* Item = &Lane->Items[EmitIndex];
    FLVRenderFlag* Flag = &Item->Flag;

    if (FLVTempIsMissing(Lane, Op.LHS)) {
        Item->IsMissing = TRUE;

        return;
    }

    FLVSelection const* Sel = FLVSelectionPtr(Lane, Op.LHS);

    Item->Kind = FLV_RENDER_KIND_FLAG;
    Flag->Ranges = ArenaPushArray(
       Lane->ResultMemPool, 
       r1u64, 
       Sel->Count
   );
    Flag->Count = Sel->Count;
    Flag->TotalDurationUSecs = 0;
    Flag->LabelStringIndex = Emit->LabelStringIndex;
    MemCpy(Flag->Ranges, Sel->V, Sel->Count * sizeof(r1u64));

    for (u64 Index = 0; Index < Sel->Count; ++Index)
        Flag->TotalDurationUSecs += Sel->V[Index].Max - Sel->V[Index].Min;
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_FLAG_SCALAR)
{
    u32 EmitIndex = FLVGetOperandIndex(Op.RHS);
    FLVEmit const* Emit = &Lane->Program->Emits[EmitIndex];
    FLVRenderItem* Item = &Lane->Items[EmitIndex];
    FLVRenderFlag* Flag = &Item->Flag;
 
    if (FLVTempIsMissing(Lane, Op.LHS)) {
        Item->IsMissing = TRUE;
 
        return;
    }
 
    b32 IsSet = FLVScalarI64(Lane, Op.LHS) != 0;
 
    Item->Kind = FLV_RENDER_KIND_FLAG;
    Flag->Ranges = ArenaPushArray(Lane->ResultMemPool, r1u64, 1);
    Flag->Count = IsSet;
    Flag->TotalDurationUSecs = IsSet ? Lane->FlightEndUSecs : 0;
    Flag->LabelStringIndex = Emit->LabelStringIndex;
    Flag->Ranges[0] = Rng((u64) 0, Lane->FlightEndUSecs);
}

INTERNAL void
FLVWriteValuePush(FLVLane* Lane, FLVOp Op, u8 Kind)
{
    FLVWriteValue Value = {};

    Value.Kind = Kind;
    Value.IsMissing = (
        Kind == FLV_ELEMENT_KIND_NONE ||
        FLVTempIsMissing(Lane, Op.LHS)
    );

    if (!Value.IsMissing) {
        if (Kind == FLV_ELEMENT_KIND_SINGLE_F64)
            Value.V.F64 = FLVScalarF64(Lane, Op.LHS);
        else
            Value.V.I64 = FLVScalarI64(Lane, Op.LHS);
    }

    FLVEmitBuildPush(
        Lane,
        &Lane->EmitBuilds[FLVGetOperandIndex(Op.RHS)],
        &Value,
        sizeof(Value)
    );
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_WRITE_I64)
{
    FLVWriteValuePush(Lane, Op, FLV_ELEMENT_KIND_SINGLE_I64);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_WRITE_F64)
{
    FLVWriteValuePush(Lane, Op, FLV_ELEMENT_KIND_SINGLE_F64);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_WRITE_NONE)
{
    FLVWriteValuePush(Lane, Op, FLV_ELEMENT_KIND_NONE);
}

// NOTE(nc): the name of a mode, from the table of the vehicle of the
//         : flight. a mode that the table does not have is its number.
INTERNAL Str8
FLVWriteModeName(FLVLane* Lane, i64 Number)
{
    u64 InfoCount = 0;
    FLIFileInfo* Infos = FLITableFromName(
        Lane->FLI,
        FileInfo,
        FILE_INFO,
        &InfoCount
    );
    FLVVehicleKind VehicleKind = FLV_VEHICLE_KIND_UNKNOWN;
    Str8 Result = {};

    if (InfoCount) {
        u64 Length = 0;
        u8* Base = FLIStrFromIndex(
            Lane->FLI,
            Infos[InfoCount - 1].VehicleStringIndex,
            &Length
        );

        VehicleKind = FLVVehicleKindFromStr(Str(Base, Length));
    }

    if (VehicleKind < FLV_VEHICLE_KIND_COUNT) {
        FLVModeName const* Names = FLV_MODE_TABLE[VehicleKind];

        for (u32 Index = 0; Index < FLV_MODE_TABLE_COUNT[VehicleKind]; ++Index) {
            if ((i64) Names[Index].Number == Number) {
                Result = Names[Index].Name;
                break;
            }
        }
    }

    if (!Result.Size)
        Result = ArenaPushStrFmt(Lane->MemPool, "MODE %lld", Number);

    return Result;
}


#define __VSN_WRITE_CHAR(X) do {                        \
    if ((u64) (BufferPtr - Buffer) < BufferCapacity) {  \
        *BufferPtr = X;                                 \
    }                                                   \
    ++BufferPtr;                                        \
    ++Result;                                           \
} while (FALSE)

#define __VSN_WRITE_STR(X, Y) do {                                                                        \
    u64 BufferAvailable = !(BufferPtr > Buffer + BufferCapacity) * (Buffer + BufferCapacity - BufferPtr); \
    u64 BufferToWrite = MIN(Y, BufferAvailable);                                                          \
    if (BufferToWrite)                                                                                    \
        MemCpy(BufferPtr, X, BufferToWrite * sizeof(*BufferPtr));                                         \
    BufferPtr += Y;                                                                                       \
    Result += Y;                                                                                          \
} while (FALSE)

// NOTE(nc): the line of a write, made as __VSNPrintf makes a string: with
//         : no buffer it counts the characters, and with a buffer it
//         : writes them. the values come from the build of the write, in
//         : the order of the format, and not from a va_list.
INTERNAL i32
FLVWritePrint(
    FLVLane* Lane,
    u8* Buffer,
    u64 BufferCount,
    Str8 Format,
    FLVWriteValue const* Values,
    u64 ValuesCount
) {
    i32 Result = 0;
    u8* BufferPtr = Buffer;
    const u64 BufferCapacity = !!Buffer * BufferCount;
    FLVWriteSpec Spec = {};
    u64 Offset = 0;
    u64 ValueIndex = 0;

    while (FLVWriteSpecNext(Format, &Offset, &Spec)) {
        if (Spec.Literal.Size)
            __VSN_WRITE_STR(Spec.Literal.Str, Spec.Literal.Size);

        if (!Spec.Conversion)
            continue;

        if (Spec.Conversion == FLV_WRITE_CONVERSION_INVALID) {
            __VSN_WRITE_CHAR('%');

            if (Spec.Width)
                __VSN_WRITE_CHAR((u8) Spec.Width);

            continue;
        }

        FLVWriteValue const* Value = (ValueIndex < ValuesCount)
            ? &Values[ValueIndex]
            : NULL;
        b32 IsF64 = (Value && Value->Kind == FLV_ELEMENT_KIND_SINGLE_F64);
        i64 Whole = 0;
        f64 Real = 0.0;
        Str8 String = "-"_s8;
        b32 IsString = TRUE;

        ++ValueIndex;

        if (Value && !Value->IsMissing) {
            Whole = IsF64 ? (i64) Value->V.F64 : Value->V.I64;
            Real = IsF64 ? Value->V.F64 : (f64) Value->V.I64;

            // NOTE(nc): a duration is microseconds, and is written in
            //         : seconds
            if (Spec.Conversion == 't')
                Real = (f64) Whole / 1000000.0;

            // NOTE(nc): a NaN is a value that is missing
            if (Real == Real) {
                IsString = (Spec.Conversion == 'm');

                if (IsString)
                    String = FLVWriteModeName(Lane, Whole);
            }
        }

        if (IsString) {
            i32 Len = (i32) String.Size;

            if (Spec.Width > Len && !Spec.HaveMinus)
                for (i32 Index = 0; Index < Spec.Width - Len; ++Index)
                    __VSN_WRITE_CHAR(' ');

            __VSN_WRITE_STR(String.Str, Len);

            if (Spec.Width > Len && Spec.HaveMinus)
                for (i32 Index = 0; Index < Spec.Width - Len; ++Index)
                    __VSN_WRITE_CHAR(' ');

            continue;
        }

        switch (Spec.Conversion) {
            default: {} break;

            case 'd':
            case 'i': {
                char TempBuffer[32];
                char* ValString = TempBuffer + sizeof(TempBuffer);
                b32 Negative = (Whole < 0);
                u64 UnsignedVal = (Negative) ? -(u64) Whole : (u64) Whole;

                ValString = U64ToCStr(UnsignedVal, ValString);

                i32 ValLen = (i32) (
                    (TempBuffer + sizeof(TempBuffer) - 1) - ValString
                );
                i32 TotalLen = ValLen + (
                    Negative || Spec.HavePlus || Spec.HaveSpace
                );

                if (!Spec.HaveMinus && !Spec.HaveZero && Spec.Width > TotalLen)
                    for (i32 Index = 0; Index < Spec.Width - TotalLen; ++Index)
                        __VSN_WRITE_CHAR(' ');

                if (Negative)
                    __VSN_WRITE_CHAR('-');
                else if (Spec.HavePlus)
                    __VSN_WRITE_CHAR('+');
                else if (Spec.HaveSpace)
                    __VSN_WRITE_CHAR(' ');

                if (!Spec.HaveMinus && Spec.HaveZero && Spec.Width > TotalLen)
                    for (i32 Index = 0; Index < Spec.Width - TotalLen; ++Index)
                        __VSN_WRITE_CHAR('0');

                __VSN_WRITE_STR(ValString, ValLen);

                if (Spec.HaveMinus && Spec.Width > TotalLen)
                    for (i32 Index = 0; Index < Spec.Width - TotalLen; ++Index)
                        __VSN_WRITE_CHAR(' ');
            } break;

            case 'f':
            case 'F': 
            case 't': {
                i32 Precision = (Spec.Precision < 0) ? 6 : Spec.Precision;
                char TempBuffer[128];
                b32 IsNegative = (Real < 0.0);
                i64 IntPart = (i64) Real;
                f64 FracPart = AbsF64(Real - (f64) IntPart);
                char* Ptr = U64ToCStr(
                    (u64) Abs64(IntPart),
                    TempBuffer + 64
                );

                if (IsNegative)
                    __VSN_WRITE_CHAR('-');

                __VSN_WRITE_STR(Ptr, (TempBuffer + 64 - 1) - Ptr);

                if (Precision > 0) {
                    __VSN_WRITE_CHAR('.');

                    for (i32 Index = 0; Index < Precision; ++Index) {
                        FracPart *= 10;

                        i32 Digit = (i32) FracPart;

                        __VSN_WRITE_CHAR('0' + Digit);
                        FracPart -= Digit;
                    }
                }
            } break;
        }
    }

    if (Buffer && BufferCount) {
        u64 FinalPosition = BufferPtr - Buffer;

        if (FinalPosition < BufferCount) {
            Buffer[FinalPosition] = 0;
        } else {
            Buffer[BufferCount - 1] = 0;
        }
    }

    return Result;
}

#undef __VSN_WRITE_CHAR
#undef __VSN_WRITE_STR

// NOTE(nc): the values of the write are in its build, in the order of the
//         : format. the line is made one time, here, so a view only shows
//         : a string.
INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_WRITE)
{
    u32 EmitIndex = FLVGetOperandIndex(Op.RHS);
    FLVEmit const* Emit = &Lane->Program->Emits[EmitIndex];
    FLVRenderItem* Item = &Lane->Items[EmitIndex];
    FLVEmitBuild* Build = &Lane->EmitBuilds[EmitIndex];
    FLVWriteValue const* Values = Build->Head
        ? (FLVWriteValue const*) Build->Head->V
        : NULL;
    u64 ValuesCount = Build->ByteCount / sizeof(FLVWriteValue);
    Str8 Format = Lane->Program->Strings[Emit->LabelStringIndex];
    i32 Size = FLVWritePrint(Lane, NULL, 0, Format, Values, ValuesCount);
    u8* Buffer = ArenaPushArray(Lane->ResultMemPool, u8, (u64) Size + 1);

    FLVWritePrint(Lane, Buffer, (u64) Size + 1, Format, Values, ValuesCount);
    Item->Kind = FLV_RENDER_KIND_TEXT;
    Item->Text = Str(Buffer, (u64) Size);
}

// NOTE(nc): the lines of a write that has ranges. the values of all the
//         : ranges are in the build of the write, one range after the
//         : other, so a line takes the values of its range. the lines
//         : are one string, with a line break between two of them.
INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_WRITE_ROWS)
{
    u32 EmitIndex = FLVGetOperandIndex(Op.RHS);
    FLVEmit const* Emit = &Lane->Program->Emits[EmitIndex];
    FLVRenderItem* Item = &Lane->Items[EmitIndex];
    FLVEmitBuild* Build = &Lane->EmitBuilds[EmitIndex];

    if (FLVTempIsMissing(Lane, Op.LHS)) {
        Item->IsMissing = TRUE;

        return;
    }

    Str8 Format = Lane->Program->Strings[Emit->LabelStringIndex];
    u64 RowCount = (u64) FLVScalarI64(Lane, Op.LHS);
    u64 ValuesCount = Build->ByteCount / sizeof(FLVWriteValue);
    u64 RowValues = RowCount ? ValuesCount / RowCount : 0;
    FLVWriteValue* Values = ArenaPushArray(
        Lane->MemPool,
        FLVWriteValue,
        ValuesCount
    );
    u64 Offset = 0;
    u64 Size = 0;
    u64 At = 0;

    for (FLVValueChunk* Chunk = Build->Head; Chunk; Chunk = Chunk->Next) {
        MemCpy((u8*) Values + Offset, Chunk->V, Chunk->Count);
        Offset += Chunk->Count;
    }

    for (u64 Row = 0; Row < RowCount; ++Row) {
        Size += (u64) FLVWritePrint(
            Lane,
            NULL,
            0,
            Format,
            Values + Row * RowValues,
            RowValues
        ) + 1;
    }

    u8* Buffer = ArenaPushArray(Lane->ResultMemPool, u8, Size + 1);

    for (u64 Row = 0; Row < RowCount; ++Row) {
        At += (u64) FLVWritePrint(
            Lane,
            Buffer + At,
            Size + 1 - At,
            Format,
            Values + Row * RowValues,
            RowValues
        );
        Buffer[At++] = '\n';
    }

    // NOTE(nc): the last line has no line break after it
    if (At)
        --At;

    Buffer[At] = 0;
    Item->Kind = FLV_RENDER_KIND_TEXT;
    Item->Text = Str(Buffer, At);
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_TABLE_COLUMN)
{
    u32 EmitIndex = FLVGetOperandIndex(Op.RHS);
    FLVTableRef* Ref = &Lane->TableRefs[EmitIndex];
    FLVTableRef* HeadRef = &Lane->TableRefs[Ref->Head];
    FLVRenderTable* Table = &Lane->Items[Ref->Head].Table;
    FLVRenderTableColumn* Column = &Table->Columns[EmitIndex - Ref->Head];
    FLVEmitBuild* Build = &Lane->EmitBuilds[EmitIndex];
    u64 Count = Lane->ChunkCount;
    u64 Position = Lane->ChunkPosition;
 
    if (HeadRef->Stamp != Lane->ChunkBase) {
        HeadRef->Stamp = Lane->ChunkBase;

        if (Table->Mask) {
            if (Lane->Mask)
                FLVMaskOrBits(Table->Mask, Position, Lane->Mask, Count);
            else
                FLVMaskSetRange(Table->Mask, Position, Count);
        }
    }
 
    if (Column->Column != FLV_COLUMN_NONE || Column->IsMissing)
        return;

    if (FLVTempIsMissing(Lane, Op.LHS)) {
        Column->IsMissing = TRUE;

        return;
    }

    u8 const* Src = (u8 const*) FLVTempPtr(Lane, Op.LHS);
    u64 Width = Column->Width;
 
    if (Lane->Mask) {
        for (u64 Index = 0; Index < Count; ++Index) {
            if (FLVMaskBit(Lane->Mask, Index)) {
                FLVEmitBuildPush(Lane, Build, Src + Index * Width, Width);
            }
        }
    } else {
        for (u64 Index = 0; Index < Count; ++Index) {
            FLVEmitBuildPush(Lane, Build, Src + Index * Width, Width);
        }
    }
}

INTERNAL
FLV_KERNEL_FUNC_SIG(FLVKernelEMIT_MISSING)
{
    Lane->Items[FLVGetOperandIndex(Op.RHS)].IsMissing = TRUE;
}

INTERNAL b32
FLVIsReduction(FLVOp Op)
{
    b32 Result = (
        (FLV_KERNEL_INFO_TABLE[Op.Kernel].Flags & FLV_KERNEL_FLAG_REDUCTION) &&
        Op.Kernel != FLV_KERNEL_SEL_FROM_MASK
    );
 
    return Result;
}
 
INTERNAL u64
FLVWindowBound(FLVLane* Lane, u64 Width)
{
    FLVColumn const* T = Lane->BaseTime;
    u64 Result = 1;
 
    for (u64 RangeIndex = 0; RangeIndex < Lane->RangesCount; ++RangeIndex) {
        r1u64 Range = Lane->Ranges[RangeIndex];
        u64 Front = Range.Min;
 
        for (u64 Index = Range.Min; Index < Range.Max; ++Index) {
            u64 Now = FLVTimeAt(T, Index);

            while (FLVTimeAt(T, Front) + Width <= Now)
                ++Front;
 
            Result = MAX(Result, Index - Front + 1);
        }
    }
 
    return Result;
}

void 
FLVKernelsRegionBegin(FLVLane* Lane, FLVRegion const* Region)
{
    // NOTE(nc): BuildSectionsBegin
    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];

        if (Op.Kernel != FLV_KERNEL_SEL_FROM_MASK)
            continue;

        FLVSelectionBuild* Build = &Lane->SelectionBuilds[
            FLVGetOperandIndex(Op.Dst)
        ];

        MemZero(Build, sizeof(*Build));
    }

   // NOTE(nc): ReductionsBegin 
    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];
 
        if (FLVIsReduction(Op)) {
            MemZero(
                &Lane->Reductions[FLVGetOperandIndex(Op.Dst)], 
                sizeof(FLVReduction)
            );
        }
    }

    // NOTE(nc): HoldsBegin
    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];

        if (
            Op.Kernel == FLV_KERNEL_LOAD_HOLD_I64 ||
            Op.Kernel == FLV_KERNEL_LOAD_HOLD_F64
        ) {
            MemZero(
                &Lane->Holds[FLVGetOperandIndex(Op.Dst)], 
                sizeof(FLVHold)
            );
        }
    }

    // NOTE(nc): WindowsBegin
    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];
 
        if (
            !(FLV_KERNEL_INFO_TABLE[Op.Kernel].Flags & FLV_KERNEL_FLAG_ROLLING)
        ) {
            continue;
        }
 
        FLVWindow* W = &Lane->Windows[FLVGetOperandIndex(Op.Dst)];
        u64 Width = FLVScalarTimeUSecs(Lane, Op.RHS);
 
        W->Capacity = Round64NextPow2(FLVWindowBound(Lane, Width));
        W->Times = ArenaPushArray(Lane->MemPool, u64, W->Capacity);
        W->Values = ArenaPushArray(Lane->MemPool, FLVNumber, W->Capacity);
    }

    // NOTE(nc): EmitsBegin
    r1u64* RegionRanges = NULL;
    u64 MaskWords = FLVSeriesWordCount(Lane->RegionSampleCount) + 1;

    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];
        u32 EmitIndex = FLVGetOperandIndex(Op.RHS);
 
        switch (Op.Kernel) {
            default: {} break;
 
            case FLV_KERNEL_EMIT_CURVE: {
                FLVEmit const* Emit = &Lane->Program->Emits[EmitIndex];
                FLVRenderItem* Item = &Lane->Items[EmitIndex];
                FLVRenderCurve* Curve = &Item->Curve;
                FLVEmitBuild* Build = &Lane->EmitBuilds[EmitIndex];

                if (!RegionRanges)
                    RegionRanges = FLVRegionRangesCpy(Lane);

                MemZero(Build, sizeof(*Build));
                Build->SourceSlot = FLVEmitSourceSlot(
                    Lane, 
                    Region, 
                    Index, 
                    Op.LHS
                );
                Build->Width = sizeof(f32);

                b32 IsDerived = (Build->SourceSlot == U32_MAX);
                b32 NeedMask = (IsDerived || Region->Mask != FLV_OPERAND_NONE);
 
                Item->Kind = FLV_RENDER_KIND_CURVE;
                Curve->Ranges = RegionRanges;
                Curve->RangesCount = Lane->RangesCount;
                Curve->Mask = NeedMask
                    ? ArenaPushArrayZero(Lane->ResultMemPool, u64, MaskWords)
                    : NULL;
                Curve->Rank = NULL;
                Curve->Values = NULL;
                Curve->SampleCount = Lane->RegionSampleCount;
                Curve->Count = 0;
                Curve->TimeColumn = Lane->Slots[Region->BaseSlot].TimeColumn;
                Curve->ValueColumn = IsDerived
                    ? FLV_COLUMN_NONE
                    : Lane->Slots[Build->SourceSlot].Column;
                Curve->LabelStringIndex = Emit->LabelStringIndex;
                Curve->Axis = Emit->Axis;

                if (!IsDerived && Lane->Columns[Build->SourceSlot].IsMissing)
                    Item->IsMissing = TRUE;
            } break;
 
            case FLV_KERNEL_EMIT_TABLE_COLUMN: {
                FLVTableRef const* Ref = &Lane->TableRefs[EmitIndex];
                FLVTableRef const* HeadRef = &Lane->TableRefs[Ref->Head];
                FLVEmit const* Emit = &Lane->Program->Emits[EmitIndex];
                FLVRenderItem* Item = &Lane->Items[Ref->Head];
                FLVRenderTable* Table = &Item->Table;
                FLVEmitBuild* Build = &Lane->EmitBuilds[EmitIndex];

                if (!RegionRanges)
                    RegionRanges = FLVRegionRangesCpy(Lane);
 
                if (Item->Kind != FLV_RENDER_KIND_TABLE) {
                    Item->Kind = FLV_RENDER_KIND_TABLE;
                    Table->Ranges = RegionRanges;
                    Table->RangesCount = Lane->RangesCount;
                    Table->Mask = (Region->Mask != FLV_OPERAND_NONE)
                        ? ArenaPushArrayZero(
                            Lane->ResultMemPool, 
                            u64, 
                            MaskWords
                        ) : NULL;
                    Table->Rank = NULL;
                    Table->Columns = ArenaPushArrayZero(
                        Lane->ResultMemPool,
                        FLVRenderTableColumn,
                        HeadRef->Count
                    );
                    Table->SampleCount = Lane->RegionSampleCount;
                    Table->RowCount = 0;
                    Table->TimeColumn = Lane->Slots[Region->BaseSlot].TimeColumn;
                    Table->ColumnCount = HeadRef->Count;
 
                    for (u32 C = 0; C < HeadRef->Count; ++C) {
                        Table->Columns[C].LabelStringIndex = Lane->Program->Emits[
                            Ref->Head + C
                        ].LabelStringIndex;
                        Table->Columns[C].Column = FLV_COLUMN_NONE;
                    }
                }
 
                FLVRenderTableColumn* Column = &Table->Columns[
                    EmitIndex - Ref->Head
                ];

                MemZero(Build, sizeof(*Build));
                Build->SourceSlot = FLVEmitSourceSlot(
                    Lane, 
                    Region, 
                    Index, 
                    Op.LHS
                );
 
                if (Build->SourceSlot != U32_MAX) {
                    FLVBoundSlot const* Slot = &Lane->Slots[Build->SourceSlot];
                    FLVColumn const* Source = &Lane->Columns[Build->SourceSlot];
 
                    Column->Column = Slot->Column;
                    Column->Kind = Source->Kind;
                    Column->Width = Source->ElementSize;
                    Column->IsMissing = Source->IsMissing;
                } else {
                    Column->Column = FLV_COLUMN_NONE;
                    Column->Kind = (Emit->ElementKind == FLV_ELEMENT_KIND_F64)
                        ? FLI_SCALAR_KIND_F64
                        : FLI_SCALAR_KIND_I64;
                    Column->Width = 8;
                    Build->Width = 8;
                }
            } break;
        }
    }
}

void 
FLVKernelsRegionEnd(FLVLane* Lane, FLVRegion const* Region)
{
    // NOTE(nc): BuildSelectionsEnd
    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];

        if (Op.Kernel != FLV_KERNEL_SEL_FROM_MASK)
            continue;

        FLVSelectionBuild* Build = &Lane->SelectionBuilds[
            FLVGetOperandIndex(Op.Dst)
        ];
        FLVSelection* Sel = FLVSelectionPtr(Lane, Op.Dst);

        if (Build->IsOpen) {
            FLVSelectionBuildPush(
                Lane, 
                Build, 
                Rng(Build->OpenStartUSecs, Build->OpenEndUSecs)
            );
            Build->IsOpen = FALSE;
        }

        *Sel = FLVSelectionFromBuild(Lane, Build);
    }

    // NOTE(nc): ReductionsEnd
    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];
 
        if (!FLVIsReduction(Op) || FLVTempIsMissing(Lane, Op.Dst))
            continue;
 
        FLVReduction const* R = &Lane->Reductions[FLVGetOperandIndex(Op.Dst)];
        FLVNumber* Dst = (FLVNumber*) FLVTempPtr(Lane, Op.Dst);
 
        if (!R->Count) {
            FLVTempSetMissing(Lane, Op.Dst);
            continue;
        }
 
        switch (Op.Kernel) {
            default: {
                ASSERT(FALSE);
            } break;
 
            case FLV_KERNEL_MEAN_I64:
            case FLV_KERNEL_MEAN_F64: {
                Dst->F64 = R->Summation.F64 / (f64) R->Count;
            } break;
 
            case FLV_KERNEL_STDDEV_I64:
            case FLV_KERNEL_STDDEV_F64: {
                f64 M = R->Summation.F64 / (f64) R->Count;
                f64 Var = R->SummationSquared / (f64) R->Count - M * M;
 
                Dst->F64 = (Var > 0.0) ? SqrtF64(Var) : 0.0;
            } break;
 
            case FLV_KERNEL_MIN_I64:
            case FLV_KERNEL_MIN_F64: {
                *Dst = R->Minimum;
            } break;
 
            case FLV_KERNEL_MAX_I64:
            case FLV_KERNEL_MAX_F64: {
                *Dst = R->Maximum;
            } break;
 
            case FLV_KERNEL_SUM_I64:
            case FLV_KERNEL_SUM_F64: {
                *Dst = R->Summation;
            } break;
 
            case FLV_KERNEL_FIRST_I64:
            case FLV_KERNEL_FIRST_F64: {
                *Dst = R->Head;
            } break;
 
            case FLV_KERNEL_LAST_I64:
            case FLV_KERNEL_LAST_F64: {
                *Dst = R->Tail;
            } break;
 
            case FLV_KERNEL_REDUCE_COUNT:
            case FLV_KERNEL_REDUCE_COUNT_F64: {
                Dst->I64 = (i64) R->Count;
            } break;
        }
    }

    // NOTE(nc): EmitsEnd
    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];
        u32 EmitIndex = FLVGetOperandIndex(Op.RHS);

        switch (Op.Kernel) {
            default: {} break;

            case FLV_KERNEL_EMIT_CURVE: {
                FLVRenderItem* Item = &Lane->Items[EmitIndex];
                FLVRenderCurve* Curve = &Item->Curve;
                FLVEmitBuild* Build = &Lane->EmitBuilds[EmitIndex];

                if (Item->IsMissing)
                    break;

                if (Curve->Mask) {
                    Curve->Rank = FLVRankFromMask(
                        Lane->ResultMemPool, 
                        Curve->Mask, 
                        Curve->SampleCount
                    );
                    Curve->Count = Curve->Rank[
                        FLVSeriesWordCount(Curve->SampleCount)
                    ];
                } else {
                    Curve->Count = Curve->SampleCount;
                }

                if (Build->SourceSlot == U32_MAX) {
                    ASSERT(Build->ByteCount == Curve->Count * sizeof(f32));

                    Curve->Values = (f32*) FLVEmitBuildFlatten(Lane, Build);
                }
            } break;

            case FLV_KERNEL_EMIT_TABLE_COLUMN: {
                FLVTableRef const* Ref = &Lane->TableRefs[EmitIndex];
                FLVRenderItem* Item = &Lane->Items[Ref->Head];
                FLVRenderTable* Table = &Item->Table;
                FLVRenderTableColumn* Column = &Table->Columns[
                    EmitIndex - Ref->Head
                ];
                FLVEmitBuild* Build = &Lane->EmitBuilds[EmitIndex];

                if (EmitIndex == Ref->Head) {
                    if (Table->Mask) {
                        Table->Rank = FLVRankFromMask(
                            Lane->ResultMemPool, 
                            Table->Mask, 
                            Table->SampleCount
                        );
                        Table->RowCount = Table->Rank[
                            FLVSeriesWordCount(Table->SampleCount)
                        ];
                    } else {
                        Table->RowCount = Table->SampleCount;
                    }
                }

                if (Column->Column == FLV_COLUMN_NONE && !Column->IsMissing)
                    Column->Values = FLVEmitBuildFlatten(Lane, Build);
            } break;
        }
    }
}

void 
FLVKernelsRangeReset(FLVLane* Lane, FLVRegion const* Region)
{
    // NOTE(nc): WindowsReset
    for (u32 Index = 0; Index < Region->OpCount; ++Index) {
        FLVOp Op = Lane->Program->Ops[Region->HeadOp + Index];
 
        if (!(FLV_KERNEL_INFO_TABLE[Op.Kernel].Flags & FLV_KERNEL_FLAG_WINDOW))
            continue;
 
        FLVWindow* W = &Lane->Windows[FLVGetOperandIndex(Op.Dst)];
 
        W->Head = 0;
        W->Count = 0;
        W->Summation.I64 = 0;
        W->HasPrev = FALSE;
    }
}

#define FLVKernelNONE              FLVKernelStub

#define X(Name, Inputs, Dst, Flags) FLVKernel##Name,
FLVKernelFuncType* const FLV_KERNEL_TABLE[FLV_KERNEL_COUNT] = {
    FLV_KERNEL_LIST(X)
};
#undef X

#undef FLVKernelNONE
