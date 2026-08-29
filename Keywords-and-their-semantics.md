# Preprocessor
The most obvious once will be:

include
if
elif
else
endif
pragma

The most obvious continuation will be define stuff

import      probably not soon XD
module
export

They will work just like in normal c++ but: probably changed so they will be cooler and simpler

Linkage

export (it is both pp and linkage, obviously)
extern (with extern "C" too)
inline
static

Constant evaluation

constexpr (constexpr variables WILL be mutable unless declared const and after constant evaluation the pointers they point to WILL be moved to static memory)
consteval (consteval variables are same as constexpr variables in normal c++)
static_assert
embed

Threading

thread_local
thread
(no async as it is easly writable with thread but:

async
await

are reserved

)

Default keywords

(important)

copy
move

(expanation: the language will NOT have operator = as an assignment, it is fucking idiotic, instead it will look like this:
 ```
    copy(int x, 5);
    copy(int y, 5);
    copy(y, x + y);
    move(int z, y);
```
it might seem strange, but I fucking hate operator = as assignment and its variations as I am a mathematician, so I decided to make your life a living hell.
Note: doing copy(x, x + y) is usually not recommended on large objects and instead it is recommended to move(x, x + y))

and
or
not
xor

match (with constexpr and consteval options)
loop (if you are wondering what is so different about loop from while, you can loop your ass)
```
cast<type, cast_type = static (can be changed to dynamic or reint, or you can make custom cast)>
```
asm (constexpr and consteval options availible, but I hope you will not use it as it will fucking kill your compile times)
static
inline
yield
await
return
auto
type
goto (with changed semantic, will come in handy when you will inevidably write a for loop)
void
class (I think I should describe it. YOU WILL NOT GET ANY STANDARD CLASSES INCLUDING INTEGERS, FLOATS unless you turn on weak mode, BUT I HOPE YOU ARE NOT WEAK)
mem (a type with given memory size. works as a memory region for Classes you will inevidably write. only mem constexpr and mem consteval can be on the stack)
error (constexpr and consteval options)
warning (constexpr and consteval options)
ptr (to deref you can just write not ptr, i know it is geneous)
namespace
default (you are definitly gonna hate writing move(MyVerGoodTypeThatDoesNotCommitWarCrimes, default), don't forget: doing copy in this scenario will probablycreate second instance of MyVerGoodTypeThatDoesNotCommitWarCrimes that will be copied by you instance of MyVerGoodTypeThatDoesNotCommitWarCrimes)
reflect_on_my_stupid_actions (basicly proposed c++ reflexr)
template
check (basicly requires from c++)

As this is the best language ever, we have no inheritence. If you need it really bad, go write some macros, it is not that hard (probably).

lambdas operate like in c++, with the exception of them taking everything literally (no offense)