# mini-brain
A mini [🧠❗](https://en.wikipedia.org/wiki/Brainfuck#:~:text=The%20language's%20name%20is%20a,the%20boundaries%20of%20computer%20programming.)compiler using C++ and LLVM

## Goals
- Learn about LLVM and code generation (IR)
- Learn basic compiler optimizations (what the LLVM functions are doing under the hood)
- Get better at C++

## Usage
Use generate the `mini-brain` executable
```console
clang++ -g main.cc `llvm-config --cxxflags --ldflags --system-libs --libs core orcjit native` -O3 -o mini-brain
```
Running this program should generate the LLVM IR of your BF code in `program.bf`

To compile the IR to a binary, use
```console
`./mini-brain | clang -x ir -`
```

Lastly, run `./a.out` to see what your program's result!
