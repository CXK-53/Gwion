# Variadic functions

> A function whoses arity is not fixed.

Well, a function that takes a fixed number of arguments, and additionnal ones.

## a simple example
``` gw
fun void variadic_test(int i, ...) {
  <<< "first argument is ", i >>>;
  vararg.start;
  <<< "\tadditionnal argument", vararg.i >>>;
  vararg.end;
}
variadic_test(1);
variadic_test(1, 2);
variadic_test(1, 2, 3);
```
