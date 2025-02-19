#pragma once

#include <llvm/ADT/MapVector.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/Casting.h>
#include <llvm/Support/SourceMgr.h>

#include <iostream>

#include "util.hpp"

class EliminateSyncPhis {

    public: 

    EliminateSyncPhis(llvm::Module &module) {
        // Start by scanning module for blocks that follow a sync.continue.
        std::set<BasicBlock*> syncBlocks;
        for (llvm::Function &func : module) {
            for (llvm::BasicBlock &bb : func) {
                if (auto *syncInst = dyn_cast<SyncInst>(bb.getTerminator())) {
                    syncBlocks.insert(syncInst->getSuccessor(0));
                }
            }
        }

        // Now look for phis: if any of the predecessors are a sync block,
        // then we will duplicate every branch of the phi into its own block, and hook up the corresponding ones.
        for (llvm::Function &func : module) {
            for (llvm::BasicBlock &bb : func) {
                for (llvm::Instruction &inst : bb) {
                    if (auto *phiInst = dyn_cast<PHINode>(&inst)) {
                        // Check if any of the incoming blocks are sync blocks.
                        bool hasSyncPred = false;
                        for (unsigned i = 0; i < phiInst->getNumIncomingValues(); i++) {
                            if (syncBlocks.find(phiInst->getIncomingBlock(i)) != syncBlocks.end()) {
                                hasSyncPred = true;
                                break;
                            }
                        }

                        if (hasSyncPred) {
                            // Duplicate the phi node and its incoming branches.
                            llvm::BasicBlock *newBB = llvm::BasicBlock::Create(module.getContext(), "sync_phi", &func);
                            llvm::PHINode *newPhi = llvm::PHINode::Create(phiInst->getType(), phiInst->getNumIncomingValues(), "sync_phi", newBB);
                            for (unsigned i = 0; i < phiInst->getNumIncomingValues(); i++) {
                                newPhi->addIncoming(phiInst->getIncomingValue(i), phiInst->getIncomingBlock(i));
                            }

                            // Hook up the new block to the sync blocks.
                            for (auto *syncBlock : syncBlocks) {
                                for (unsigned i = 0; i < phiInst->getNumIncomingValues(); i++) {
                                    if (syncBlock == phiInst->getIncomingBlock(i)) {
                                        syncBlock->getTerminator()->setSuccessor(i, newBB);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
};