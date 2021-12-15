#include "mul.h"
#include "gtest/gtest.h"

using namespace std;
using namespace mlir::linnea::expr;

TEST(Chain, MCP) {
  ScopedContext ctx;
  auto *A = new Operand("A1", {30, 35});
  auto *B = new Operand("A2", {35, 15});
  auto *C = new Operand("A3", {15, 5});
  auto *D = new Operand("A4", {5, 10});
  auto *E = new Operand("A5", {10, 20});
  auto *F = new Operand("A6", {20, 25});
  auto G = mul(A, mul(B, mul(C, mul(D, mul(E, F)))));
  long result = G->getMCPFlops();
  EXPECT_EQ(result, 30250);
}

TEST(Chain, MCPVariadicMul) {
  ScopedContext ctx;
  auto *A = new Operand("A1", {30, 35});
  auto *B = new Operand("A2", {35, 15});
  auto *C = new Operand("A3", {15, 5});
  auto *D = new Operand("A4", {5, 10});
  auto *E = new Operand("A5", {10, 20});
  auto *F = new Operand("A6", {20, 25});
  auto G = mul(A, B, C, D, E, F);
  long result = G->getMCPFlops();
  EXPECT_EQ(result, 30250);
}

// Expect cost to be n^2 * m * 2 -> 20 * 20 * 15 * 2
TEST(Chain, Cost) {
  ScopedContext ctx;
  auto *A = new Operand("A", {20, 20});
  auto *B = new Operand("B", {20, 15});
  auto *E = mul(A, B);
  long result = E->getMCPFlops();
  EXPECT_EQ(result, (20 * 20 * 15) << 1);
}

// Expect cost to be n^2 * m as A is lower triangular
// lower triangular are square, verify?
TEST(Chain, CostWithProperty) {
  ScopedContext ctx;
  auto *A = new Operand("A", {20, 20});
  auto *B = new Operand("B", {20, 15});
  A->setProperties({Expr::ExprProperty::LOWER_TRIANGULAR});
  auto *M = mul(A, B);
  long result = M->getMCPFlops();
  EXPECT_EQ(result, (20 * 20 * 15));
}

TEST(Chain, kernelCostWhenSPD) {
  ScopedContext ctx;
  auto *A = new Operand("A", {20, 20});
  auto *B = new Operand("B", {20, 15});
  A->setProperties({Expr::ExprProperty::FULL_RANK});
  long cost = 0;
  auto E = mul(mul(trans(A), A), B);
  cost = E->getMCPFlops();
  EXPECT_EQ(cost, 22000);
}

TEST(Chain, CountFlopsIsSPD) {
  ScopedContext ctx;
  auto *A = new Operand("A", {20, 20});
  auto *B = new Operand("B", {20, 15});
  A->setProperties({Expr::ExprProperty::FULL_RANK});
  auto E = mul(mul(trans(A), A), B);
  auto result = E->getMCPFlops();
  EXPECT_EQ(result, 22000);
}

TEST(Chain, CountFlopsIsSymmetric) {
  ScopedContext ctx;
  auto *A = new Operand("A", {20, 20});
  auto *B = new Operand("B", {20, 15});
  auto E = mul(mul(trans(A), A), B);
  auto result = E->getMCPFlops();
  EXPECT_EQ(result, 22000);
  auto F = mul(mul(A, trans(A)), B);
  result = F->getMCPFlops();
  EXPECT_EQ(result, 22000);
  auto G = mul(A, trans(A), B);
  result = G->getMCPFlops();
  EXPECT_EQ(result, 22000);
}

// See table: https://arxiv.org/pdf/1804.04021.pdf
TEST(Chain, costBlas) {
  ScopedContext ctx;
  auto *X = new Operand("A", {30, 20});
  auto *Y = new Operand("B", {30, 40});
  // GEMM
  auto *Z = mul(X, Y);
  auto flops = Z->getMCPFlops();
  EXPECT_EQ(flops, 2 * 20 * 30 * 40);

  X = new Operand("A", {30, 30});
  X->setProperties({Expr::ExprProperty::LOWER_TRIANGULAR});
  Y = new Operand("B", {30, 20});
  // TRMM
  Z = mul(X, Y);
  flops = Z->getMCPFlops();
  EXPECT_EQ(flops, 30 * 30 * 20);
}

TEST(Chain, LinneaTest0) {
  /*

  """
      algorithm0(ml0::Array{Float64,2}, ml1::Array{Float64,2},
  ml2::Array{Float64,2}, ml3::Array{Float64,2})

  Compute
  X = (A B C D).

  Requires at least Julia v1.0.

  # Arguments
  - `ml0::Array{Float64,2}`: Matrix A of size 100 x 90.
  - `ml1::Array{Float64,2}`: Matrix B of size 90 x 90 with property SPD.
  - `ml2::Array{Float64,2}`: Matrix C of size 90 x 80.
  - `ml3::Array{Float64,2}`: Matrix D of size 80 x 70.
  """
  function algorithm0(ml0::Array{Float64,2}, ml1::Array{Float64,2},
  ml2::Array{Float64,2}, ml3::Array{Float64,2}) # cost: 3.4e+06 FLOPs # A: ml0,
  full, B: ml1, full, C: ml2, full, D: ml3, full ml4 = Array{Float64}(undef, 90,
  70) # tmp3 = (C D) gemm!('N', 'N', 1.0, ml2, ml3, 0.0, ml4)

      # A: ml0, full, B: ml1, full, tmp3: ml4, full
      ml5 = Array{Float64}(undef, 90, 70)
      # tmp5 = (B tmp3)
      symm!('L', 'L', 1.0, ml1, ml4, 0.0, ml5)

      # A: ml0, full, tmp5: ml5, full
      ml6 = Array{Float64}(undef, 100, 70)
      # tmp6 = (A tmp5)
      gemm!('N', 'N', 1.0, ml0, ml5, 0.0, ml6)

      # tmp6: ml6, full
      # X = tmp6
      return (ml6)
  */

  ScopedContext ctx;
  auto *A = new Operand("A", {100, 90});
  auto *B = new Operand("B", {90, 90});
  B->setProperties({Expr::ExprProperty::SPD});
  auto *C = new Operand("C", {90, 80});
  auto *D = new Operand("D", {80, 70});
  auto *X = mul(A, B, C, D);
  auto flops = X->getMCPFlops();
  EXPECT_EQ(flops, 3402000);
}

/*
TEST(Chain, Factorized) {
  ScopedContext ctx;
  auto *A = new Operand("A", {20, 20});
  A->setProperties({Expr::ExprProperty::FULL_RANK});
  auto *L = new Operand("L", {20, 20});
  L->setProperties({Expr::ExprProperty::LOWER_TRIANGULAR,
Expr::ExprProperty::FACTORED}); auto *R = mul(trans(A), inv(trans(L)), inv(L),
A); auto f = R->getMCPFlops(); EXPECT_EQ(f, 1);
}
*/
