/************************************************************************
 ************************************************************************
    FAUST compiler
    Copyright (C) 2003-2015 GRAME, Centre National de Creation Musicale
    ---------------------------------------------------------------------
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 ************************************************************************
 ************************************************************************/

#ifndef _FIR_INTERPRETER_BYTECODE_H
#define _FIR_INTERPRETER_BYTECODE_H

#include <vector>
#include <string>
#include <math.h>
#include <assert.h>
#include <iostream>

#include "fir_opcode.hh"

static inline std::string replaceChar1(std::string str, char ch1, char ch2)
{
    for (unsigned int i = 0; i < str.length(); ++i) {
        if (str[i] == ch1) {
            str[i] = ch2;
        }
    }
    return "\"" + str + "\"";
}

// Bytecode definition

typedef long double quad;

template <class T>
struct FIRBlockInstruction;

template <class T>
struct FIRBasicInstruction : public FIRInstruction {

    Opcode fOpcode;
    int fIntValue;
    T fRealValue;
    int fOffset1;
    int fOffset2;
    
    FIRBlockInstruction<T>* fBranch1;
    FIRBlockInstruction<T>* fBranch2;
    
    FIRBasicInstruction(Opcode opcode, 
                        int val_int, T val_real, 
                        int off1, int off2,
                        FIRBlockInstruction<T>* branch1, 
                        FIRBlockInstruction<T>* branch2) 
                        :fOpcode(opcode), fIntValue(val_int), fRealValue(val_real),
                        fOffset1(off1), fOffset2(off2),
                        fBranch1(branch1), fBranch2(branch2)
    {}
    
    FIRBasicInstruction(Opcode opcode, 
                        int val_int, T val_real) 
                        :fOpcode(opcode), fIntValue(val_int), fRealValue(val_real),
                        fOffset1(0), fOffset2(0),
                        fBranch1(0), fBranch2(0)
    {}
    
    FIRBasicInstruction(Opcode opcode, 
                        int val_int, T val_real, int off1, int off2)
                        :fOpcode(opcode), fIntValue(val_int), fRealValue(val_real),
                        fOffset1(off1), fOffset2(off2),
                        fBranch1(0), fBranch2(0)
    {}
    
    FIRBasicInstruction(Opcode opcode) 
                        :fOpcode(opcode), fIntValue(0), fRealValue(0),
                        fOffset1(0), fOffset2(0),
                        fBranch1(0), fBranch2(0)
    {}
    
     
    virtual ~FIRBasicInstruction()
    {
        delete fBranch1;
        delete fBranch2;
    }
    
    int size()
    {
        int branches = std::max(((fBranch1) ? fBranch1->size() : 0), ((fBranch2) ? fBranch2->size() : 0));
        return (branches > 0) ? branches : 1;
    }
    
    // TODO : fix some remaining issues : do a "all instructions" trace comparation with FIRInterpreter::ExecuteBlock
    
    void stackMove(int& int_index, int& real_index)
    {
        //----------------
        // Int operations
        //----------------
        
        //std::cout << "fOpcode " << fOpcode << " " << gFIRInstructionTable[fOpcode] << std::endl;
        
        if (fOpcode == kIntValue) {
            int_index++;
        } else if (fOpcode == kLoadInt) {
            int_index++;
        } else if (fOpcode == kLoadIndexedInt) {
            // Nothing
        } else if (fOpcode == kStoreInt) {
            int_index--;
        } else if (fOpcode == kStoreIndexedInt) {
            int_index -= 2;
        } else if (fOpcode == kIf) {
            
            // cond
            int_index--;
            
            int branch1_int_index = 0;
            int branch1_real_index = 0;
            assert(fBranch1);
            fBranch1->stackMove(branch1_int_index, branch1_real_index);
            
            int branch2_int_index = 0;
            int branch2_real_index = 0;
            assert(fBranch2);
            fBranch2->stackMove(branch2_int_index, branch2_real_index);
            
            // Adjust indexes
            int_index += std::max(branch1_int_index, branch2_int_index);
            real_index += std::max(branch1_real_index, branch2_real_index);
            
        } else if (fOpcode == kCastInt) {
            int_index++; real_index--;
        } else if (fOpcode == kCastIntHeap) {
            int_index++;
        } else if ((fOpcode == kAddInt) || (fOpcode == kSubInt) ||
                (fOpcode == kMultInt) || (fOpcode == kDivInt) ||
                (fOpcode == kRemInt) || (fOpcode == kLshInt) ||
                (fOpcode == kRshInt) || (fOpcode == kGTInt) ||
                (fOpcode == kLTInt) || (fOpcode == kGEInt) ||
                (fOpcode == kLEInt) || (fOpcode == kEQInt) ||
                (fOpcode == kNEInt) || (fOpcode == kANDInt) ||
                (fOpcode == kORInt) || (fOpcode == kXORInt)) {
            int_index--;
        } else if ((fOpcode == kAddIntHeap) || (fOpcode == kSubIntHeap) ||
                (fOpcode == kMultIntHeap) || (fOpcode == kDivIntHeap) ||
                (fOpcode == kRemIntHeap) || (fOpcode == kLshIntHeap) ||
                (fOpcode == kRshIntHeap) || (fOpcode == kGTIntHeap) ||
                (fOpcode == kLTIntHeap) || (fOpcode == kGEIntHeap) ||
                (fOpcode == kLEIntHeap) || (fOpcode == kEQIntHeap) ||
                (fOpcode == kNEIntHeap) || (fOpcode == kANDIntHeap) ||
                (fOpcode == kORIntHeap) || (fOpcode == kXORIntHeap) ||
                
                (fOpcode == kAddIntDirect) || (fOpcode == kSubIntDirect) ||
                (fOpcode == kMultIntDirect) || (fOpcode == kDivIntDirect) ||
                (fOpcode == kRemIntDirect) || (fOpcode == kLshIntDirect) ||
                (fOpcode == kRshIntDirect) || (fOpcode == kGTIntDirect) ||
                (fOpcode == kLTIntDirect) || (fOpcode == kGEIntDirect) ||
                (fOpcode == kLEIntDirect) || (fOpcode == kEQIntDirect) ||
                (fOpcode == kNEIntDirect) || (fOpcode == kANDIntDirect) ||
                (fOpcode == kORIntDirect) || (fOpcode == kXORIntDirect) ||
                
                (fOpcode == kSubIntDirectInvert) || (fOpcode == kDivIntDirectInvert) ||
                (fOpcode == kRemIntDirectInvert) || (fOpcode == kLshIntDirectInvert) ||
                (fOpcode == kRshIntDirectInvert) || (fOpcode == kGTIntDirectInvert) ||
                (fOpcode == kLTIntDirectInvert) || (fOpcode == kGEIntDirectInvert) ||
                (fOpcode == kLEIntDirectInvert)) {
            int_index++;
        } else if (fOpcode == kLoop) {
            
            fBranch1->stackMove(int_index, real_index);
     
        //-----------------
        // Real operations
        //-----------------
        
        } else if (fOpcode == kRealValue) {
            real_index++;
        } else if (fOpcode == kLoadReal) {
            real_index++;
        } else if (fOpcode == kLoadInput) {
            int_index--; real_index++;
        } else if (fOpcode == kLoadIndexedReal) {
            int_index--; real_index++;
        } else if (fOpcode == kStoreReal) {
            real_index--;
        } else if (fOpcode == kStoreOutput) {
            int_index--; real_index--;
        } else if (fOpcode == kStoreIndexedReal) {
            int_index--; real_index--;
        } else if (fOpcode == kCastReal) {
            int_index--; real_index++;
        } else if (fOpcode == kCastRealHeap) {
            real_index++;
        } else if ((fOpcode == kAddReal) || (fOpcode == kSubReal) ||
                   (fOpcode == kMultReal) || (fOpcode == kDivReal) ||
                   (fOpcode == kRemReal)) {
            real_index--;
        } else if ((fOpcode == kGTReal) || (fOpcode == kLTReal) ||
                   (fOpcode == kGEReal) || (fOpcode == kLEReal) ||
                   (fOpcode == kEQReal) || (fOpcode == kNEReal)) {
            int_index++; real_index -= 2;
        } else if ((fOpcode == kAddRealHeap) || (fOpcode == kSubRealHeap) ||
                   (fOpcode == kMultRealHeap) || (fOpcode == kDivRealHeap) ||
                   (fOpcode == kRemRealHeap)) {
            real_index++;
        } else if ((fOpcode == kGTRealHeap) || (fOpcode == kLTRealHeap) ||
                   (fOpcode == kGERealHeap) || (fOpcode == kLERealHeap) ||
                   (fOpcode == kEQRealHeap) || (fOpcode == kNERealHeap)) {
            int_index++;
        } else if ((fOpcode == kAddRealDirect) || (fOpcode == kSubRealDirect) ||
                   (fOpcode == kMultRealDirect) || (fOpcode == kDivRealDirect) ||
                   (fOpcode == kRemRealDirect)) {
            real_index++;
        } else if ((fOpcode == kGTRealDirect) || (fOpcode == kLTRealDirect) ||
                   (fOpcode == kGERealDirect) || (fOpcode == kLERealDirect) ||
                   (fOpcode == kEQRealDirect) || (fOpcode == kNERealDirect)) {
            int_index++;
        } else if ((fOpcode == kSubRealDirectInvert) || (fOpcode == kDivRealDirectInvert) ||
                   (fOpcode == kRemRealDirectInvert)) {
            real_index++;
        } else if ((fOpcode == kGTRealDirectInvert) || (fOpcode == kLTRealDirectInvert) ||
                   (fOpcode == kGERealDirectInvert) || (fOpcode == kLERealDirectInvert)) {
            int_index++;
        } else if ((fOpcode == kAtan2f) || (fOpcode == kFmodf) ||
                   (fOpcode == kPowf) || (fOpcode == kMax) ||
                   (fOpcode == kMaxf) || (fOpcode == kMin) ||
                   (fOpcode == kMinf)) {
            real_index++;
        } else if ((fOpcode == kAtan2fHeap) || (fOpcode == kFmodfHeap) ||
                   (fOpcode == kPowfHeap) || (fOpcode == kMaxHeap) ||
                   (fOpcode == kMaxfHeap) || (fOpcode == kMinHeap) ||
                   (fOpcode == kMinfHeap)) {
            real_index++;
        } else if ((fOpcode == kAtan2fDirect) || (fOpcode == kFmodfDirect) ||
                   (fOpcode == kPowfDirect) || (fOpcode == kMaxDirect) ||
                   (fOpcode == kMaxfDirect) || (fOpcode == kMinDirect) ||
                   (fOpcode == kMinfDirect)) {
            real_index++;
        } else if ((fOpcode == kAtan2fDirectInvert) || (fOpcode == kFmodfDirectInvert)
                   || (fOpcode == kPowfDirectInvert)) {
        } else {
            // No move
        }
    }
    
    void write(std::ostream* out)
    {
        *out << "opcode " << fOpcode << " " << gFIRInstructionTable[fOpcode]
        << " int " << fIntValue
        << " real " << fRealValue
        << " offset1 " << fOffset1
        << " offset2 " << fOffset2
        << std::endl;
        // If select/if/loop : write branches
        if (fBranch1) fBranch1->write(out);
        if (fBranch2) fBranch2->write(out);
    }
    
    FIRBasicInstruction<T>* copy()
    {
        return new FIRBasicInstruction<T>(fOpcode, fIntValue, fRealValue, fOffset1, fOffset2,
                                          ((fBranch1) ? fBranch1->copy() : 0),
                                          ((fBranch2) ? fBranch2->copy() : 0));
    }
    
};

template <class T>
struct FIRUserInterfaceInstruction : public FIRInstruction {

    Opcode fOpcode;
    int fOffset;
    std::string fLabel;
    std::string fKey;
    std::string fValue;
    T fInit;
    T fMin;
    T fMax;
    T fStep;
    
    FIRUserInterfaceInstruction(Opcode opcode, int offset,
                                const std::string& label,
                                const std::string& key,
                                const std::string& value, T init, T min, T max, T step)
        :fOpcode(opcode), fOffset(offset), fLabel(label), fKey(key), fValue(value), fInit(init), fMin(min), fMax(max), fStep(step)
    {}
    
    FIRUserInterfaceInstruction(Opcode opcode, int offset, const std::string& label, T init, T min, T max, T step)
        :fOpcode(opcode), fOffset(offset), fLabel(label), fInit(init), fMin(min), fMax(max), fStep(step)
    {}
    
    FIRUserInterfaceInstruction(Opcode opcode)
        :fOpcode(opcode), fOffset(0), fLabel(""), fKey(""), fValue(""), fInit(0), fMin(0), fMax(0), fStep(0)
    {}
    
    FIRUserInterfaceInstruction(Opcode opcode, const std::string& label)
        :fOpcode(opcode), fOffset(0), fLabel(label), fKey(""), fValue(""), fInit(0), fMin(0), fMax(0), fStep(0)
    {}
    
    FIRUserInterfaceInstruction(Opcode opcode, int offset, const std::string& label)
        :fOpcode(opcode), fOffset(offset), fLabel(label), fKey(""), fValue(""), fInit(0), fMin(0), fMax(0), fStep(0)
    {}
     
    FIRUserInterfaceInstruction(Opcode opcode, int offset, const std::string& label, T min, T max)
        :fOpcode(opcode), fOffset(offset), fLabel(label), fKey(""), fValue(""), fInit(0), fMin(min), fMax(max), fStep(0)
    {}
    
    FIRUserInterfaceInstruction(Opcode opcode, int offset, const std::string& key, const std::string& value)
        :fOpcode(opcode), fOffset(offset), fLabel(""), fKey(key), fValue(value), fInit(0), fMin(0), fMax(0), fStep(0)
    {}
    
    virtual ~FIRUserInterfaceInstruction()
    {}
    
    void write(std::ostream* out)
    {
        *out << "opcode " << fOpcode << " " << gFIRInstructionTable[fOpcode]
        << " offset " << fOffset
        << " label " << replaceChar1(fLabel, ' ', '_') << " key " << replaceChar1(fKey, ' ', '_') << " value " << replaceChar1(fValue, ' ', '_')
        << " init " << fInit << " min " << fMin << " max " << fMax << " step " << fStep << std::endl;
    }
    
};

#define InstructionIT typename std::vector<FIRBasicInstruction<T>* >::iterator
#define UIInstructionIT typename std::vector<FIRUserInterfaceInstruction<T>* >::iterator

template <class T>
struct FIRUserInterfaceBlockInstruction : public FIRInstruction {

    std::vector<FIRUserInterfaceInstruction<T>*> fInstructions;
     
    virtual ~FIRUserInterfaceBlockInstruction()
    {
        UIInstructionIT it;
        for (it = fInstructions.begin(); it != fInstructions.end(); it++) {
            delete(*it);
        }
    }
     
    void push(FIRUserInterfaceInstruction<T>* inst) { fInstructions.push_back(inst); }
    
    void write(std::ostream* out)
    {
        *out << "block_size " << fInstructions.size() << std::endl;
        UIInstructionIT it;
        for (it = fInstructions.begin(); it != fInstructions.end(); it++) {
            (*it)->write(out);
        }
    }
    
};

template <class T>
struct FIRBlockInstruction : public FIRInstruction {
    
    std::vector<FIRBasicInstruction<T>*> fInstructions;
    
    virtual ~FIRBlockInstruction()
    {
        InstructionIT it;
        for (it = fInstructions.begin(); it != fInstructions.end(); it++) {
            delete (*it);
        }
    }
    
    void push(FIRBasicInstruction<T>* inst) { fInstructions.push_back(inst); }
    
    void write(std::ostream* out)
    {
        *out << "block_size " << fInstructions.size() << std::endl;
        InstructionIT it;
        for (it = fInstructions.begin(); it != fInstructions.end(); it++) {
            (*it)->write(out);
        }
    }
    
    void stackMove(int& int_index, int& real_index)
    {
        std::cout << "FIRBlockInstruction::stackMove" << std::endl;
        InstructionIT it;
        int tmp_int_index = 0;
        int tmp_real_index = 0;
        for (it = fInstructions.begin(); it != fInstructions.end(); it++) {
            (*it)->stackMove(tmp_int_index, tmp_real_index);
            (*it)->write(&std::cout);
            std::cout << "int_stack_index " << tmp_int_index << " real_stack_index " << tmp_real_index << std::endl;
            assert(tmp_int_index >= 0 && tmp_real_index >= 0);
            int_index = std::max(int_index, tmp_int_index);
            real_index = std::max(real_index, tmp_real_index);
        }
    }
    
    FIRBlockInstruction<T>* copy()
    {
        FIRBlockInstruction<T>* block = new FIRBlockInstruction<T>();
        InstructionIT it;
        for (it = fInstructions.begin(); it != fInstructions.end(); it++) {
            block->push((*it)->copy());
        }
        return block;
    }
  
    int size()
    {
        int size = 0;
        InstructionIT it;
        for (it = fInstructions.begin(); it != fInstructions.end(); it++) {
            size += (*it)->size();
        }
        return size;
    }
};

#endif