/*
 * Copyright (c) 2016-2018,2019 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder. You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
 * Copyright (c) 2013 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu/o3/rename_map.hh"

#include <vector>

#include "cpu/o3/dyn_inst.hh"
#include "cpu/o3/regfile.hh"
#include "cpu/reg_class.hh"
#include "debug/Rename.hh"
#include "debug/BranchS.hh"

namespace gem5
{

namespace o3
{

SimpleRenameMap::SimpleRenameMap() : freeList(NULL)
{
}


void
SimpleRenameMap::init(const RegClass &reg_class, SimpleFreeList *_freeList)
{
    assert(freeList == NULL);
    assert(map.empty());

    map.resize(reg_class.numRegs());
    freeList = _freeList;
}

SimpleRenameMap::RenameInfo
SimpleRenameMap::rename(const RegId& arch_reg)
{
    PhysRegIdPtr renamed_reg;
    // Record the current physical register that is renamed to the
    // requested architected register.
    PhysRegIdPtr prev_reg = map[arch_reg.index()];

    if (arch_reg.is(InvalidRegClass)) {
        assert(prev_reg->is(InvalidRegClass));
        renamed_reg = prev_reg;
    } else if (prev_reg->getNumPinnedWrites() > 0) {
        // Do not rename if the register is pinned
        assert(arch_reg.getNumPinnedWrites() == 0);  // Prevent pinning the
                                                     // same register twice
        DPRINTF(Rename, "Renaming pinned reg, numPinnedWrites %d\n",
                prev_reg->getNumPinnedWrites());
        renamed_reg = prev_reg;
        renamed_reg->decrNumPinnedWrites();
    } else {
        renamed_reg = freeList->getReg();
        map[arch_reg.index()] = renamed_reg;
        renamed_reg->setNumPinnedWrites(arch_reg.getNumPinnedWrites());
        renamed_reg->setNumPinnedWritesToComplete(
            arch_reg.getNumPinnedWrites() + 1);
    }

    DPRINTF(Rename, "Renamed reg %d to physical reg %d (%d) old mapping was"
            " %d (%d)\n",
            arch_reg, renamed_reg->flatIndex(), renamed_reg->flatIndex(),
            prev_reg->flatIndex(), prev_reg->flatIndex());

    return RenameInfo(renamed_reg, prev_reg);
}


/**** UnifiedRenameMap methods ****/

void
UnifiedRenameMap::init(const BaseISA::RegClasses &regClasses,
        PhysRegFile *_regFile, UnifiedFreeList *freeList)
{
    regFile = _regFile;

    for (int i = 0; i < renameMaps.size(); i++)
        renameMaps[i].init(*regClasses.at(i), &(freeList->freeLists[i]));
}

bool
UnifiedRenameMap::canRename(DynInstPtr inst) const
{
    for (int i = 0; i < renameMaps.size(); i++) {
        if (inst->numDestRegs((RegClassType)i) >
                renameMaps[i].numFreeEntries()) {
            return false;
        }
    }
    return true;
}

bool
RenameUnifiedRenameMap::canRename(DynInstPtr inst) const
{
    if (NoBrS()) {
        return map->canRename(inst);
    } else {
        if (inst->readPredS())
                return mapBrS->canRename(inst);
            else
                return map->canRename(inst);
    }
}

void
RenameUnifiedRenameMap::init(UnifiedRenameMap* mapPtr)
{
    map = mapPtr;
}

/** Initializes rename map with given parameters. */
void
RenameUnifiedRenameMap::init(const BaseISA::RegClasses &regClasses,
            PhysRegFile *_regFile, UnifiedFreeList *freeList)
{
    map->init(regClasses, _regFile, freeList);
}

/**
 * Tell rename map to get a new free physical register to remap
 * the specified architectural register. This version takes a
 * RegId and reads the  appropriate class-specific rename table.
 * @param arch_reg The architectural register id to remap.
 * @return A RenameInfo pair indicating both the new and previous
 * physical registers.
 */
RenameUnifiedRenameMap::RenameInfo
RenameUnifiedRenameMap::rename(const RegId& arch_reg, bool taken)
{
    if (NoBrS()) {
        return map->rename(arch_reg);
    } else {
        if (taken) {
            DPRINTF(BranchS, "MapBrS rename reg[%i]\n", arch_reg);
            return mapBrS->rename(arch_reg);
        }
        else {
            DPRINTF(BranchS, "Map rename reg[%i]\n", arch_reg);
            return map->rename(arch_reg);
        }
    }
}

/**
 * Look up the physical register mapped to an architectural register.
 * This version takes a flattened architectural register id
 * and calls the appropriate class-specific rename table.
 * @param arch_reg The architectural register to look up.
 * @return The physical register it is currently mapped to.
 */
PhysRegIdPtr
RenameUnifiedRenameMap::lookup(const RegId& arch_reg, bool taken) const
{
    if (NoBrS()) {
        return map->lookup(arch_reg);
    } else {
        if (taken) {
            DPRINTF(BranchS, "mapBrS lookup reg[%i]\n", arch_reg);
            return mapBrS->lookup(arch_reg);
        }
        else {
            DPRINTF(BranchS, "map lookup reg[%i]\n", arch_reg);
            return map->lookup(arch_reg);
        }
    }
}

/**
 * Update rename map with a specific mapping.  Generally used to
 * roll back to old mappings on a squash.  This version takes a
 * flattened architectural register id and calls the
 * appropriate class-specific rename table.
 * @param arch_reg The architectural register to remap.
 * @param phys_reg The physical register to remap it to.
 */
void
RenameUnifiedRenameMap::setEntry(const RegId& arch_reg, PhysRegIdPtr phys_reg, bool taken)
{
    if (NoBrS()) {
        return map->setEntry(arch_reg, phys_reg);
    } else {
        if (taken) {
            DPRINTF(BranchS, "squashing mapBrS entry, arch_reg %d, phys_reg %d\n",
                    arch_reg, phys_reg->flatIndex());
            return mapBrS->setEntry(arch_reg, phys_reg);
        } else {
            DPRINTF(BranchS, "squashing map entry, arch_reg %d, phys_reg %d\n",
                    arch_reg, phys_reg->flatIndex());
            return map->setEntry(arch_reg, phys_reg);
        }
    }
}

/**
 * Return the minimum number of free entries across all of the
 * register classes.  The minimum is used so we guarantee that
 * this number of entries is available regardless of which class
 * of registers is requested.
 */
unsigned
RenameUnifiedRenameMap::numFreeEntries() const
{
    return map->numFreeEntries();
}

unsigned
RenameUnifiedRenameMap::numFreeEntries(RegClassType type) const
{
    return map->numFreeEntries(type);
}

void
RenameUnifiedRenameMap::initBrS()
{
    assert(NoBrS());
    mapBrS = new UnifiedRenameMap(*map);
    assert(map != mapBrS);
    printMap();
    printMapBrS();
    // fatal("break point\n");
}

// remove mapBrS due to previous branch misprediction
void
RenameUnifiedRenameMap::squash(bool taken)
{
    assert(mapBrS);
    printMap();
    printMapBrS();
    if (taken) {
        DPRINTF(BranchS, "BranchS taken, swap two rename maps\n");
        std::swap(map, mapBrS);
    }
    delete mapBrS;
    mapBrS = nullptr;
    printMap();
}

void
SimpleRenameMap::printMap(PhysRegFile *regFile) const
{
    for (int i = 0; i < numArchRegs(); i++) {
        DPRINTF(BranchS, "ArchReg[%i] => PhyReg[%i] = %#x\n",
                i, map[i]->index(), regFile->getReg(map[i]));
    }
}

void
UnifiedRenameMap::printMap() const
{
    DPRINTF(BranchS, "Int Regs:\n");
    renameMaps[IntRegClass].printMap(regFile);
    DPRINTF(BranchS, "Float Regs:\n");
    renameMaps[FloatRegClass].printMap(regFile);
    DPRINTF(BranchS, "Vec Regs:\n");
    renameMaps[VecElemClass].printMap(regFile);
    DPRINTF(BranchS, "CC Regs:\n");
    renameMaps[CCRegClass].printMap(regFile);
}

void
RenameUnifiedRenameMap::printMap() const
{
    DPRINTF(BranchS, "Regular Rename Map:\n");
    map->printMap();
}

void
RenameUnifiedRenameMap::printMapBrS() const
{
    assert(mapBrS);
    DPRINTF(BranchS, "BrS Rename Map:\n");
    mapBrS->printMap();
}

} // namespace o3
} // namespace gem5
