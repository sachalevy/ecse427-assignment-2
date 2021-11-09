# ecse427 - assignment - 2

Every test was successfully run multiple times (race conditions making outputs unpredictable should be avoided). Note that both `sut.c` and `sut.h` have been modified, and that the header file `queue.h` has been added in order to provide support for queues. You will find below instructions to run the five tests provided as support material.

## Tests 1-4
For tests 1 to 4 you may compile and run the tests as follows:
```bash
gcc test{i}.c sut.c -pthread
./a.out
```
> Note that {i} should be replaced by the index of the test.

## Test 5
For this test you will need to manually set the value `C_EXECS_COUNT` defined in `sut.h` to 2 in order to create two C-Exec threads. You can then proceed to compile and run the test file as with previous tests:
```bash
gcc test5.c sut.c -pthread
./a.out
```