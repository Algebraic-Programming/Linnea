//===- LinneaConvertToLinalg.cpp ---------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Standalone/LinneaAttributes.h"
#include "Standalone/LinneaOps.h"
#include "Standalone/LinneaPasses.h"
#include "Standalone/LinneaUtils.h"
#include "mlir/Dialect/StandardOps/Transforms/FuncConversions.h"
#include "mlir/Transforms/DialectConversion.h"

#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "llvm/Support/Debug.h"

using namespace mlir;
using namespace mlir::linnea;
// using namespace mlir::linnea::utils;

#define GEN_PASS_CLASSES
#include "Standalone/LinneaPasses.h.inc"

#define DEBUG_TYPE "linnea-convert-to-linalg"
#define DBGS() (llvm::dbgs() << "[" DEBUG_TYPE << "]: ")

namespace {

static inline bool isMLIRFloatType(mlir::Type t) {
  return t.isF16() || t.isF32() || t.isF64();
}

static inline bool isMLIRIntType(mlir::Type t) {
  return t.isInteger(2) || t.isInteger(4) || t.isInteger(8) ||
         t.isInteger(16) || t.isInteger(32) || t.isInteger(64);
}

template <typename FOpTy, typename IOpTy>
static Value buildBinaryOpFromValues(OpBuilder builder, Value left, Value right,
                                     Location loc, Type t) {
  if (isMLIRFloatType(t))
    return builder.create<FOpTy>(loc, left, right);
  else if (isMLIRIntType(t))
    return builder.create<IOpTy>(loc, left, right);
  else
    llvm_unreachable("unsupported type");
}

// Emit linalg matrix op. Optimization (i.e., matrix-chain
// reordering happen at the symbolic level).
static Value emitLinalgMatrix(MulOpLow op, ValueRange operands,
                              ConversionPatternRewriter &rewriter,
                              TypeConverter *typeConverter,
                              ResultRange results) {
  assert(operands.size() == 2 && "expect two operands");
  assert(results.size() == 1 && "expect one output");
  auto loc = op->getLoc();
  auto ctx = op->getContext();

  Value left = operands[0];
  Value right = operands[1];
  RankedTensorType outputType =
      typeConverter->convertType(results[0].getType()).cast<RankedTensorType>();

  Value buffer = rewriter.create<linalg::InitTensorOp>(
      loc, outputType, ArrayRef<Value>({}),
      rewriter.getI64ArrayAttr(outputType.getShape()));
  // TODO: fix me.
  // assert(outputType.getElementType().isa<FloatType>() && "only float for
  // now");

  // auto zero = FloatAttr::get(outputType.getElementType(), 0);
  // DenseElementsAttr init = DenseElementsAttr::get(outputType, zero);
  // Value splatTensor = rewriter.create<arith::ConstantOp>(loc, outputType,
  // init);

  // build affine map for matmul.
  using MapList = ArrayRef<ArrayRef<AffineExpr>>;
  auto infer = [](MapList m) { return AffineMap::inferFromExprList(m); };
  AffineExpr m, n, k;
  bindDims(ctx, m, n, k);
  auto matMulMap = infer({{m, k}, {k, n}, {m, n}});

  // iterator for matmul.
  llvm::SmallVector<StringRef, 3> iter = {"parallel", "parallel", "reduction"};

  return rewriter
      .create<linalg::GenericOp>(
          loc, TypeRange{outputType}, ValueRange{left, right}, buffer,
          matMulMap, iter,
          [&](OpBuilder &nestedBuilder, Location nestedLoc, ValueRange args) {
            assert(args.size() == 3 && "matmul expects 3 args");
            Value mul = buildBinaryOpFromValues<arith::MulFOp, arith::MulIOp>(
                nestedBuilder, args[0], args[1], nestedLoc,
                outputType.getElementType());
            nestedBuilder.create<linalg::YieldOp>(nestedLoc, mul);
          })
      ->getResult(0);
}

/// Linnea conversion rule for MulOpLow.
class MulOpLowering : public OpConversionPattern<MulOpLow> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(MulOpLow op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    ValueRange operands = {adaptor.a(), adaptor.b()};
    Value result = emitLinalgMatrix(op, operands, rewriter, getTypeConverter(),
                                    op->getResults());
    assert(result != nullptr && "must be non null");
    rewriter.replaceOp(op, result);
    return success();
  }
};

/// Linnea conversion rule for FillOp.
class FillOpLowering : public OpConversionPattern<FillOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(FillOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOpWithNewOp<linalg::FillOp>(op, adaptor.value(),
                                                adaptor.output());
    return success();
  }
};

/// Linnea conversion rule for InitOp.
class InitOpLowering : public OpConversionPattern<InitOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(InitOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resType = op.getType();
    auto encoding = getLinneaMatrixEncoding(resType);
    if (!encoding)
      return failure();

    auto linneaType = resType.cast<MatrixType>();
    // TODO: ensure that op.size() == linneaType.dims().
    MemRefType memTp =
        MemRefType::get(linneaType.getDims(), linneaType.getElementType());
    Location loc = op->getLoc();
    Value memref = rewriter.create<memref::AllocOp>(loc, memTp);
    RankedTensorType builtinTensor = RankedTensorType::get(
        linneaType.getDims(), linneaType.getElementType());
    Value tensor =
        rewriter.create<bufferization::ToTensorOp>(loc, builtinTensor, memref);
    RankedTensorType builtinTensorWithProperty =
        RankedTensorType::get(linneaType.getDims(), linneaType.getElementType(),
                              linneaType.getProperty());
    rewriter.replaceOpWithNewOp<CastFromBuiltinTensorOp>(
        op, builtinTensorWithProperty, tensor);
    return success();
  }
};

/// Linnea conversion for PrintOp.
class PrintOpLowering : public OpConversionPattern<PrintOp> {
public:
  using OpConversionPattern::OpConversionPattern;
  LogicalResult
  matchAndRewrite(PrintOp op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    Type resType = op.source().getType();
    auto encoding = getLinneaMatrixEncoding(resType);
    if (!encoding)
      return failure();
    Location loc = op->getLoc();
    auto linneaType = resType.cast<MatrixType>();
    RankedTensorType castedTensorType =
        RankedTensorType::get(linneaType.getDims(), linneaType.getElementType(),
                              linneaType.getProperty());
    Value castedToTensor =
        rewriter.create<ToBuiltinTensorOp>(loc, castedTensorType, op.source());
    castedTensorType = RankedTensorType::get(linneaType.getDims(),
                                             linneaType.getElementType());
    castedToTensor = rewriter.create<CastToBuiltinTensorOp>(
        loc, castedTensorType, castedToTensor);
    MemRefType memrefType =
        MemRefType::get(linneaType.getDims(), linneaType.getElementType());
    Value memrefVal = rewriter.create<bufferization::ToMemrefOp>(
        loc, memrefType, castedToTensor);
    // use the print in the vector dialect.
    VectorType vecType =
        VectorType::get(linneaType.getDims(), linneaType.getElementType());
    Value zero = rewriter.create<arith::ConstantIndexOp>(loc, 0);
    // Value minusOne = rewriter.create<arith::ConstantFloatOp>(
    //    loc, -1.0, linneaType.getElementType());
    Value minusOne =
        rewriter.create<arith::ConstantOp>(loc, rewriter.getF32FloatAttr(-1));
    Value vecRead = rewriter.create<vector::TransferReadOp>(
        loc, vecType, memrefVal, ArrayRef<Value>{zero, zero}, minusOne);
    rewriter.replaceOpWithNewOp<vector::PrintOp>(op, vecRead);
    return success();
  }
};

// Populate patterns
void populateLinneaToLinalgPattern(RewritePatternSet &patterns,
                                   TypeConverter &converter) {
  patterns.add<FillOpLowering, MulOpLowering, InitOpLowering, PrintOpLowering>(
      converter, patterns.getContext());
}

static void setupTypeConversion(ConversionTarget &target,
                                TypeConverter &typeConverter) {
  target.addLegalOp<ToBuiltinTensorOp>();
  typeConverter.addConversion([](MatrixType type) -> RankedTensorType {
    return RankedTensorType::get(type.getDims(), type.getElementType(),
                                 type.getProperty());
  });
  typeConverter.addTargetMaterialization([](OpBuilder &builder, TensorType type,
                                            ValueRange inputs,
                                            Location loc) -> Value {
    assert(inputs.size() == 1);
    assert(inputs[0].getType().isa<MatrixType>());
    return builder.create<ToBuiltinTensorOp>(loc, type, inputs[0]);
  });
  auto sourceMaterialization = [](OpBuilder &builder, Type type,
                                  ValueRange inputs, Location loc) -> Value {
    assert(inputs.size() == 1);
    assert(inputs[0].getType().isa<TensorType>());
    return builder.create<FromBuiltinTensorOp>(loc, type, inputs[0]);
  };
  typeConverter.addSourceMaterialization(sourceMaterialization);
  typeConverter.addArgumentMaterialization(sourceMaterialization);
}

// In a finalizing conversion, we know that all of the source types have been
// converted to the destination types, so the materialization becomes an
// identity.
template <typename OpTy>
class FinalizeMaterialization : public OpConversionPattern<OpTy> {
public:
  using OpConversionPattern<OpTy>::OpConversionPattern;
  using OpAdaptor = typename OpTy::Adaptor;
  LogicalResult
  matchAndRewrite(OpTy op, OpAdaptor adaptor,
                  ConversionPatternRewriter &rewriter) const override {
    rewriter.replaceOp(op, adaptor.getOperands()[0]);
    return success();
  }
};

template <typename OpTy>
static void setupFinalization(ConversionTarget &target,
                              RewritePatternSet &patterns,
                              TypeConverter &typeConverter) {
  target.addIllegalOp<OpTy>();
  patterns.add<FinalizeMaterialization<OpTy>>(typeConverter,
                                              patterns.getContext());
}

template <typename OpTy, typename OpTy2, typename... OpTys>
static void setupFinalization(ConversionTarget &target,
                              RewritePatternSet &patterns,
                              TypeConverter &typeConverter) {
  setupFinalization<OpTy>(target, patterns, typeConverter);
  setupFinalization<OpTy2, OpTys...>(target, patterns, typeConverter);
}

struct ConvertToLinalg : public LinneaConvertToLinalgBase<ConvertToLinalg> {
  void runOnFunction() override {

    RewritePatternSet patterns(&getContext());
    TypeConverter typeConverter;
    ConversionTarget target(getContext());

    typeConverter.addConversion([](Type type) { return type; });
    setupTypeConversion(target, typeConverter);

    target.addLegalDialect<linalg::LinalgDialect>();

    setupFinalization<ToBuiltinTensorOp, FromBuiltinTensorOp>(target, patterns,
                                                              typeConverter);

    // If all result types are legal, and all block arguments are legal, then
    // all types in the program are legal.
    //
    // We also check that the operand types are legal to avoid creating invalid
    // IR. For example, this prevents the patterns from updating
    // the types of the operands to a return op without updating the enclosing
    // function.
    target.markUnknownOpDynamicallyLegal(
        [&](Operation *op) { return typeConverter.isLegal(op); });

    populateLinneaToLinalgPattern(patterns, typeConverter);

    if (failed(applyFullConversion(getFunction(), target, std::move(patterns))))
      signalPassFailure();
  }
};

} // namespace

std::unique_ptr<OperationPass<FuncOp>>
mlir::linnea::createConvertLinneaToLinalgPass() {
  return std::make_unique<ConvertToLinalg>();
}
