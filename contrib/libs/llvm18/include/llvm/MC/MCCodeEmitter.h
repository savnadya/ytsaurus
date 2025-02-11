#pragma once

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif

//===- llvm/MC/MCCodeEmitter.h - Instruction Encoding -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCCODEEMITTER_H
#define LLVM_MC_MCCODEEMITTER_H

namespace llvm {

class MCFixup;
class MCInst;
class MCSubtargetInfo;
class raw_ostream;
template<typename T> class SmallVectorImpl;

/// MCCodeEmitter - Generic instruction encoding interface.
class MCCodeEmitter {
protected: // Can only create subclasses.
  MCCodeEmitter();

public:
  MCCodeEmitter(const MCCodeEmitter &) = delete;
  MCCodeEmitter &operator=(const MCCodeEmitter &) = delete;
  virtual ~MCCodeEmitter();

  /// Lifetime management
  virtual void reset() {}

  /// Append the prefixes of given instruction to the code buffer.
  ///
  /// \param Inst a single low-level machine instruction.
  /// \param CB code buffer
  virtual void emitPrefix(const MCInst &Inst, SmallVectorImpl<char> &CB,
                          const MCSubtargetInfo &STI) const {}
  /// Encode the given \p Inst to bytes and append to \p CB.
  virtual void encodeInstruction(const MCInst &Inst, SmallVectorImpl<char> &CB,
                                 SmallVectorImpl<MCFixup> &Fixups,
                                 const MCSubtargetInfo &STI) const = 0;
};

} // end namespace llvm

#endif // LLVM_MC_MCCODEEMITTER_H

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
