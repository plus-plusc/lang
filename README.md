# Overview

++C is compiled garbage-free mediokerly typed programming language that offers you basically every functionality you want BUT there is nothing out of the box
For example: There are no if / else statements, you have to write them by yourselves using match and macros (i know, convinient)
Another exaple would be lack of assignment operator, if you want to assign something you have to either move it or copy it
Also, there is no continue or break so if you are stuck in the loop it is your responsibility to make condition in for loop mutable and put goto at the end of the loop
Possible implementation of the for loop assuming if is defined:
```
#define for(declaration; condition; increment; body):                   \
{                                                                       \
    copy(bool wasUsed, false);                                          \
    loop (condition) {                                                  \
        _start: { if (!wasUsed) { increment; copy(wasUsed, true); } }   \
        body                                                            \
        _end: { copy(condition, false); }                               \
    }                                                                   \
}

#define continue: goto _start
#define break: goto _end

// possible use
void func() {
    for (copy(int i, 0); i < 10; ++i;
        bomb("Aliens, because they are not human"); 
    )       // error: cannot change immutable condition
    for (copy(int i, 0), copy(bool cond, true); cond; []() { ++i; copy(cond, i < 10); };
        bomb("Aliens, because they are not human");                                 
    )       // a normal program that does not commit war crimes
}
```

If you think the func looks like shit, you should remember that you see what you are.