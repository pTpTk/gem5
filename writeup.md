This is a development log. I also want the document to read coherently, so I'll write the doc in sections but still label the date.

2023/09/22
## Motivation
Gem5 101 homework 4 mentioned a [paper](https://arxiv.org/abs/1411.1460) on branch avoiding algorithms. The idea is that some hard-to-predict branches hurt performance due to mispredictions. The paper suggests using cmov instead of branches. My idea is to implement a special type of branch instruction that signals the processor to speculatively execute down both paths.

The homework offers a POC program. I started the whole idea by running the code on my machine. After cleaning up the dilapidated m5 api, I got the following results:

| branch | 15.978s |
| cmov | 8.484s |

Promising results! And I know, my PC is pretty fast.

## Stepping 1

### Gem5 X86 Implementation
In gem5 the conditional branch instructions have both isControl and isCondControl flags set. The definitions are in the microop wrip. Creatively, I named my flag isCondControlS, where S stands for special. I defined a new microop wrips, which sets the flag. The goal of this stage is to detect the isCondControlS flag in the fetch stage and print a message.

### Adding instruction to the ISA
According to objdump, the hard-to-predict branch is a JGE/JNL. The instruction is 2 Bytes and the opcode is 0x7D. Therefore I should find an available 1-Byte opcode slot in the decode file.The X86 ISA looks full... So I took out the x87 instructions. The new opcode should be 0xED. I vaguely remember reading something about puting hex string in C using asm() function, but I can't find the webpage anymore... I'll change the hex file directly.

### A closer look at the POC code
When I was trying to identify the branch instruction used, I examined the POC code much closer and I realized that the two implementations differs more than just the branch/cmov place. The branch code looks like this:
```
int cv = CCid[v];
for (auto u : Neighbors[v])
{
    int cu = CCid[u];
    if (cu < cv)
        CCid[v] = cu;
}
```
And the cmov code looks like this:
```
int& cv = CCid[v];
for (auto u : Neighbors[v])
{
    int cu = CCid[u];
    cmp cu cv
    cmovnae cu cv
}
```
In the cmov code, the variable cv is a reference. When I changed the branch code to use reference, the execution time became about 8.5s...

HMMMMMMMMMMmmmmmmmmmm....

Maybe the reference hints the compiler to place the CCid[v] into a register? And the value read and write pair just results in two memory operations? I suppose this is a programming tip and potentially I can try to add this optimization into compilers?

I'll keep doing this project since the main point is to familiarize myself with gem5 and researching.

2023/09/26
### Propagating the flag to C code
I added the flag in StaticInstFlags.py and created corresponding static and dynamic instruction methods. Then in the fetch stage I would issue a fatal message upon seeing the flag.

### Binary file revisited
The new instruction should be 0xDD instead of 0xED. Now stepping 1 is complete!

2023/10/06
## Stepping 2
The goal of this step is to fetch instructions from both paths.

### Change the fetch stage
Fetching is done in lookupAndUpdateNextPC(). The class needs a structure holding the branchS status. Specifically, it records if there is a branchS in progress, which path is executing, the next_pc for both paths. The dynamic instruction generated needs a field indicating if the instruction is fetched following branchS.

### Branch prediction complications
Fetching istructions from both paths requires BTB hits. Currently I'm mimicing regular branch code for not taken in that case. This definitely needs more work.

The branch prediction is also complaining upon seeing a branchS. I'll take a look tomorrow.

2023/10/07
### Branch prediction complications continued
btbUpdate() does not update BTB lol. Removing the unnecessary stuff solves the issue. With that said, since BTB does not have the target cached at first, I need to make sure the "not taken prediction" case works and instructions are fetched sequentially.

### Update BTB
The misprediction squashing in EX stage isn't triggered upon branchS. A closer look at the program shows that the EX stage thinks the branch always goes down the not taken path.

2023/10/08
### Gem5 Debug
I forgot to test the new instruction with Gem5 before. The branch was always not taken in simulation, resulting in wrong output. The issue is in the ISA generation. Before I replaced the x87 instructions with the special branch. The execution still breaks after I replace the x87 with regular branch. Fortunately not all regular branch instructions are used in this binary, and the program is executing properly after I replaced JNP with JNLS.

2023/10/11
### Working Stepping 2
Using the regular commit to fetch feedback signals I am able to update the BTB, and the fetch stage is bringing instructions from both paths now. With that said, maybe it is beneficial to put branchS target decode in fetch stage since this is the affected stage. Also with one less scenario hopefully the implementation would be easier. I'll keep the BTB stuff as a separate version.

### Stepping 2 bug fix
The next_pc obtained by blindly advancing the previous pc is not accurate. They have a huge stepping, so after every new instruction is fetched, the next_pc stored in the local unit needs to be updated to the current inst->pcState().

2023/10/12
## Stepping 3
Stepping 3 should add the duplicated rename map into the architecture. This should be the biggest change to the CPU.

### Rename map wrapper
I created a wrapper holding two UnifiedRenameMap pointers. The wrapper has all the api methods provided by UnifiedRenameMap and when there is no BranchS it calls the UnifiedRenameMap methods. Upon BranchS, it should duplicate the regular UnifiedRenameMap and alternate between using the two rename maps. The without branchS version works now.

2023/10/13
### Rename map wrapper continued
I added the second UnifiedRenameMap into the wrapper. Each rename map depends on three objects: RegClass, PhysRegFile and UnifiedFreeList. The three objects are passed in as pointers. The two rename maps in my design should share one free list and one RegClass obj. I'm not sure if I need to handle physical register file (misc regs) differently. I'll deal with it when it comes up. The logic is pretty straight forward besides that and Stepping 3 is complete.

## Stepping 4
Stepping 4 should handle commit/squash behavior.

### IEW to commit message
A new information channel (toCommit->squashBrS\[tid\]) is added. This should be enough?

2023/10/17
### Stepping 4 breakdown
The commit/squash step is more complicated than I thought. Every stage, and every queue has its own squash behavior that requires changes.

### ROB squash
Commit stage calls the ROB squash, which now sets the instructions as squashed and ready-to-commit down the mispredicted path. A temperary work around is added, where the ROB squashes all instructions at once instead of following the squash width parameter.

### Commit squash
Commit squash involves mostly forwarding information down to previous stages. I copied most of the information flow down for now (Might need to change this further). I also added a squashBrS signal. I'll set this to true along with squash.

2023/10/18
### Fetch, decode squash
Initially I wanted to squash half of fetch queue similar to ROB. The work in progress is in branch branchS_fetch_squashBrS. A closer look at the code showed that I can use the fetch and hopefully decode squash logic as is. In order to do that, I need to modify the commit squash logic to provide the youngest valid pc. This might hurt the performance? Also the commit/ROB modification is done on the shortcut code, need to fix this later. Decode looks ok too.