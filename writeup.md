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