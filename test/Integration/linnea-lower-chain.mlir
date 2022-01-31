// RUN: ulimit -s 16000
// RUN: g++ -std=c++11 %testdir/chain-ref.cpp -o chain
// RUN: ./chain &> chain.ref.out
//
// RUN: standalone-opt %s \
// RUN: --comprehensive-properties-propagation --linnea-func-type-conversion \
// RUN: --convert-linnea-to-linalg --convert-linnea-to-loops \ 
// RUN: --linnea-finalize-func-type-conversion --canonicalize --linalg-bufferize \ 
// RUN: --func-bufferize --arith-bufferize --tensor-bufferize \
// RUN: --std-bufferize --finalizing-bufferize --convert-linalg-to-loops --convert-vector-to-scf \
// RUN: --convert-scf-to-std --convert-arith-to-llvm --convert-vector-to-llvm \
// RUN: --convert-memref-to-llvm --convert-std-to-llvm --reconcile-unrealized-casts | \
// RUN: mlir-cpu-runner \
// RUN:  -e entry -entry-point-result=void  \
// RUN: -shared-libs=%llvmlibdir/libmlir_c_runner_utils%shlibext | \
// RUN: FileCheck %s
//
// RUN: standalone-opt %s \
// RUN: --comprehensive-properties-propagation --linnea-func-type-conversion \
// RUN: --convert-linnea-to-linalg --convert-linnea-to-loops \ 
// RUN: --linnea-finalize-func-type-conversion --canonicalize --linalg-bufferize \ 
// RUN: --func-bufferize --arith-bufferize --tensor-bufferize \
// RUN: --std-bufferize --finalizing-bufferize --convert-linalg-to-loops --convert-vector-to-scf \
// RUN: --convert-scf-to-std --convert-arith-to-llvm --convert-vector-to-llvm \
// RUN: --convert-memref-to-llvm --convert-std-to-llvm --reconcile-unrealized-casts | \
// RUN: mlir-cpu-runner \
// RUN:  -e entry -entry-point-result=void  \
// RUN: -shared-libs=%llvmlibdir/libmlir_c_runner_utils%shlibext &> chain.mlir.out
//
// RUN: diff chain.ref.out chain.mlir.out
// RUN: rm -f chain.ref.out
// RUN: rm -f chain.mlir.out
// RUN: rm -rf %testdir/chain

module {
  func @entry() {
  
    // A1.
    %fA1 = arith.constant 1 : i32
    %c30 = arith.constant 30 : index
    %c35 = arith.constant 35 : index
    %A1 = linnea.init [%c30, %c35] : !linnea.matrix<#linnea.property<["general"]>, [30, 35], i32>
    %Af1 = linnea.fill(%fA1, %A1) : i32, !linnea.matrix<#linnea.property<["general"]>, [30, 35], i32>

    // A2.
    %fA2 = arith.constant 2 : i32
    %c15 = arith.constant 15 : index
    %A2 = linnea.init [%c35, %c15] : !linnea.matrix<#linnea.property<["general"]>, [35, 15], i32>
    %Af2 = linnea.fill(%fA2, %A2) : i32, !linnea.matrix<#linnea.property<["general"]>, [35, 15], i32>

    // A3.
    %fA3 = arith.constant 3 : i32
    %c5 = arith.constant 5 : index
    %A3 = linnea.init [%c15, %c5] : !linnea.matrix<#linnea.property<["general"]>, [15, 5], i32>
    %Af3 = linnea.fill(%fA3, %A3) : i32, !linnea.matrix<#linnea.property<["general"]>, [15, 5], i32>
    
    // A4.
    %fA4 = arith.constant 4 : i32
    %c10 = arith.constant 10 : index
    %A4 = linnea.init [%c5, %c10] : !linnea.matrix<#linnea.property<["general"]>, [5, 10], i32>
    %Af4 = linnea.fill(%fA4, %A4) : i32, !linnea.matrix<#linnea.property<["general"]>, [5, 10], i32>
    
    // A5.
    %fA5 = arith.constant 5 : i32
    %c20 = arith.constant 20 : index
    %A5 = linnea.init [%c10, %c20] : !linnea.matrix<#linnea.property<["general"]>, [10, 20], i32>
    %Af5 = linnea.fill(%fA5, %A5) : i32, !linnea.matrix<#linnea.property<["general"]>, [10, 20], i32>
    
    // A6.
    %fA6 = arith.constant 6 : i32
    %c25 = arith.constant 25 : index
    %A6 = linnea.init [%c20, %c25] : !linnea.matrix<#linnea.property<["general"]>, [20, 25], i32>
    %Af6 = linnea.fill(%fA6, %A6) : i32, !linnea.matrix<#linnea.property<["general"]>, [20, 25], i32>

    // Too much stack allocation. To run use ulimit -s 16000 
    %0 = linnea.equation {
      %1 = linnea.mul.high %Af1, %Af2, %Af3, %Af4, %Af5, %Af6 :
        !linnea.matrix<#linnea.property<["general"]>, [30, 35], i32>,
        !linnea.matrix<#linnea.property<["general"]>, [35, 15], i32>,
        !linnea.matrix<#linnea.property<["general"]>, [15, 5], i32>,
        !linnea.matrix<#linnea.property<["general"]>, [5, 10], i32>,
        !linnea.matrix<#linnea.property<["general"]>, [10, 20], i32>,
        !linnea.matrix<#linnea.property<["general"]>, [20, 25], i32> -> !linnea.term
      linnea.yield %1 : !linnea.term
    }
   
    // CHECK: ( ( 378000000, 
    linnea.print %0 : !linnea.term  
    return 
  }
}
