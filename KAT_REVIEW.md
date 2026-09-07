## Josh notes

- kAlign on arena might want to be 128b given arm
- try threadlocal in sparearena. sparearena is fine, arenavector must go, arena isgenerally ok except for free(). probably should all be in stack instead of using heap
  - kat generally has 2 arenas, persistent data and stretch memory, simplifies lifetimes a lot
- open() in parse file should try mapped files
exceptions in parse_file are no go
  - unwind ops etc
  - fnoexceptions
- should be using a file mapping instead of read()ing
- case stmts can be maps to function ptrs
- const char* instead of string view for beatmapheader -- stores a bit less data. char arrays if possible.
- sample set can be an enum
- suboptimal memory layout of the beatmap header, maybe misaligned on cache boundaries, altho theres no u16 to fill it with
- unlikely can be risky across archs
- TpShapeCache is ginormous
  - Instead of storing an array rows[16] we can store 16 commas, nondig, len, simd_tails etc. inline contigously as arrays -- "structured array form". better for alignment and cache coherency (accesses you want are right beside eachother, so they stay in cache)
- tp_shape_insert is pretty good. L269-275 can probably be SIMD (shuffle)
  - when processor does a read, it tries to exec as much as it can at once. add/mov together (out of order execution). it works because they track dependencies at the micro-code level. you don't want to break the dependency chain.
- memcpy calls at the bottom of tp_shape_insert seem fast, but the c runtime kinda trolls you with, which is that these call into another dll, long jmp, very likely not in cache, not in icache, inlining is impossible.
  - beyond inlining itself, it makes it so things cant be unrolled etc.
- with template instantiations, with the small funcs like the parse_{section}_line

- split_kv depending howoften its called you might want to look at putting this into vector parse loop, integrating it as part of the optimized parser
  - josh: might be scalar code

- make_lane_masks
  - kat hates them in consteval functions. he tends to write a lil c program that gens the c++ and pastes it in. it brings compile times down
- cpu features
  - __asm__ and __cpuid -- what compiler are we on? this assumes g++
  - use an assembler like gas to support inline assembly on other compilers like msvc
- he likes KvEntry -- although name should be hashed and compared so it can be faster. then they can be SIMD'd easier too!
- L135 on parse_kv_line is confusing.
  - this func is weird. alignment is also undefined behavior possibly here because of type transmutation we're doing here
- diagnostics global thing synchronized, you can publish and consume events. like analytics

- never open() and close(), always map files. one problem is page vaults. you can load
  - only use open() when you are opening shit tons of files and possibly poll them
  - with io_cp and io_uring you can get more value out of file handles

- This stuff `constexpr bool kDirect = direct_vector_writes_v<decltype(Map::timing_points)>;` and `if constexpr (kDirect) {` we should always use arena memory.

- __attribute__((noinline)) void grow sliders
  - because of some weird arena vector thing. Arena stuff can make this so much better
  - Whole VectorSink needs to just disappear

- parse_hitobjects_section #ifdef OSU_SIMD then if(use_simd) is GEEKIN and just use arenas.

- Ship multiple archs in the binary to avoid the runtime dynamic dispatch
  - multiple dlls for each arch, then have a stub "the executable" dynamically load the right arch dll based on the processors capabilities through load
  - statically link everything EXCEPT the things that should be swappable. ONLY SIMD kernels need to be swappable, ALL the other c++ code can be statically linked within our codebase.

- HitConsts
  - very slow big ass object for every parse slider lenght because it does a lot of calculations, we should NOT be init'ing this every time we call parse slider length

- in parse_point_pair L147 store is dependendcy blocking; esp a problem if you have a mispredicted branch
- after_y load on 191 is purging stuff out of registers that we dont realy want to purge at a loop end. the compiler MIGHT optimize it
  - you want to make sure at the end of the loop so your data is set up for the re-entry of the loop
   - which is why he personaly declares all variables outside of a loop

- parse_point_pair we can fold into a single compare
  - all the depednecny chains make it unlikely to be the fastest way to do this
  - everything depends on the last thing, the process is going to be stopping a lot

- unconditional divide in timing.hpp, L92 can be before before all the instructions, stupid ordering

- basically all the memchr() can probably be some SIMD mask thing

- t4 in timing.hpp is unused (cringe)

- string compares in if, it's gonna deref, parse it into a char, etc.
  - c has a contract with strings where they always exist in .rodata


- arena design for beamtap:
  - startup arena
  - one beatmap files data
    - give them a copy function



https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/arena.hpp#L23
(Arena) we just allocated a full page, the malloc here is not needed.

When allocating an array or list using an arena, you should first populate the array on the stack & then copy to arena memory.

If that will not suffice and  you really need to reallocate, you should simply just keep a linked list of arena pages and make sure that u just plop array bytes in there contigiously.

ArenaVector just needs to go.  See above.

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/arena.hpp#L170C4-L170C65
thread_local (Uses TLS [thread local storage] - always thread-safe)

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/arena.hpp#L170C4-L170C65
May vary per target - arenas should always align to the width of a cache line.


https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/src/backend.cpp#L212C1-L212C69
We should create a read-only file mapping here.  The file will be mapped into our address space as virtual memory; it will not yet be resident in RAM.  When one of its pages are dereferenced it becomes memory resident.
Exceptions WTF?!


https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/parser.hpp#L155

[Section::None] = handle_none,
[Section::Editor] = parse_editor_line,
[Section::Metadata] = parse_metadata_line,


Could probably be threaded?

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/beatmap_header.hpp#L12

Enums where possible; maybe look into using char* or char[] for strings here?


https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/hitobjects.hpp#L281C20-L281C35

Always benchmark when you use likely and unlikely, and just prefer not to use them

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/timing.hpp#L222

SOA or Structures of Arrays form is a perfect fit here!  It ensures that data is more likely to be laid out in a desirable form in cache prioritizing contagious accesses and allowing the cpu to prefetch.


struct TpShapeCache  {
  u64 commas[16];
  u16 nondig[16];
  // ....
};


https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/timing.hpp#L222

This is a _shuf SIMD instruction on x86.

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/timing.hpp#L279

Likely has to resolve memcpy through indirect ptr stub.  Calls to the C standard runtime will never be inlined too - inhibiting compiler optimizations!!!

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/document_sections.hpp#L7

These functions really don't want to be functions!  They take references and data as if they're just in the local function.  They're also one line lol.

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/text.hpp#L12

No clue how often this is called; may be worth instead directly integrating into the vectorized parse components

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/src/cpu_features.hpp#L40

asm volatile()
__asm__ __volatile__
__asm {}


What compiler are we on?

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/metadata.hpp#L27

FNV hash the string; can be trivially compared with one operations, you don't have the over head of stalling to read uncached data.

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/metadata.hpp#L135C1-L135C66

What the fuck?

uint32_t* malformed = nullptr?

Rewrite this func please.////

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/io.hpp#L1

Use file mappings here.

Ppl state pagefaults as perf concern; just prefetch and lock your pages lil bro.  VM is cool cuz u can load 5tb into ur ram and well it lies so u dont use 5tb of ram.  Lazy loading.

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/timing_section.hpp#L59

No lazy loading weird metaprogramming stuff.  Just write to arena memory; it's cheap.  Lil vro though was the big std....




https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/vector_sink.hpp#L13

This needs to disappear.  Manage pooled arena lifetimes and memory like a sane human being.

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/vector_sink.hpp#L135

we're being trolled..

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/vector_storage.hpp

Arenas we don't need this... :)

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/vector_sink.hpp#L154

Arenas here will save you.  And you broke it with weird vector usage...

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/slider.hpp#L66

default constructs HitConsts per call.  at lesat 7 calls to inline asm vpbroadcastb + mm_setr!

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/slider.hpp#L144

 This is dependency blocked per the store to memory.

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/slider.hpp#L121

we compute four masks here where (crack) pipe and comma are consumed only as sep plus two single-bit tests; folding them into 1 comp is possible on the bit vectors but they would alias 1 and < ... check?

We have a lot of dependencies here too.  We're going to break out-of-order execution,

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/timing.hpp#L129

Uncond divide that should not be here

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/timing.hpp#L92

This is independent of the really complex shit... we early exit... after the intensive code....

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/timing.hpp#L22C53-L22C79

memchr called 6 times ?

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/timing.hpp#L147C1-L147C37

We just don't use it?

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/hitobjects.hpp#L281C20-L281C35

Really should be simpler.

https://github.com/cmyui/fast-osu-beatmap-parser/blob/master/include/fosu/internal/events.hpp#L28

These are going to be slow string compares.
