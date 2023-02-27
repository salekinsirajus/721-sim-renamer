# Project Description
This is a class project for ECE 721: Advanced Microarchitecture,
taught at NC State's ECE Department by Dr. Eric Rotenberg.

The code developed here is implemented as a "fill-in-the-blanks"
module - the function definitions and expectations are provided
initially.

## How to Run
Unfortunately, it is not possible to build and run this code without
the rest of the executable. That said, you can use the `Makefile`
to build the executable with `make clean; make build` command.

## Implementation Brief
The code here implements a renamer stage in a classical out-of-order
superscalar pipeline simulator, called `721sim`. This is developed
by Dr. Eric Rotenberg.

The renamer stage uses a variable sized Active List, Free List, an
Architectural Map Table(AMT) and the Rename Map Table(RMT). The
exception recovery and branch misprediction recovery is achieved
by using a checkpoint bit vector, and Shadow Map Table, which takes
a snapshop of the architectural registers(RMT) at every branch instruction.

## ISA and Tools
We use the [SPEC2006 Benchmark](https://www.spec.org/cpu2006/) to test
our simulator implementation. This simulator models the RISC-V ISA.
