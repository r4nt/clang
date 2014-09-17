// RUN: %clang_cc1 -triple i386-apple-darwin9 -verify %s

// <rdar://problem/12415959>
// rdar://problem/11846140
// rdar://problem/17476970

typedef unsigned int u_int32_t;
typedef u_int32_t uint32_t;

typedef unsigned long long u_int64_t;
typedef u_int64_t uint64_t;

typedef float __m128 __attribute__ ((vector_size (16)));
typedef float __m256 __attribute__ ((vector_size (32)));

__m128 val128;
__m256 val256;

int func1() {
  // Error out if size is > 32-bits.
  uint32_t msr = 0x8b;
  uint64_t val = 0;
  __asm__ volatile("wrmsr"
                   :
                   : "c" (msr),
                     "a" ((val & 0xFFFFFFFFUL)), // expected-error {{invalid input size for constraint 'a'}}
                     "d" (((val >> 32) & 0xFFFFFFFFUL)));

  // Don't error out if the size of the destination is <= 32 bits.
  unsigned char data;
  unsigned int port;
  __asm__ volatile("outb %0, %w1" : : "a" (data), "Nd" (port)); // No error expected.

  __asm__ volatile("outb %0, %w1" : : "R" (val), "Nd" (port)); // expected-error {{invalid input size for constraint 'R'}}
  __asm__ volatile("outb %0, %w1" : : "q" (val), "Nd" (port)); // expected-error {{invalid input size for constraint 'q'}}
  __asm__ volatile("outb %0, %w1" : : "Q" (val), "Nd" (port)); // expected-error {{invalid input size for constraint 'Q'}}
  __asm__ volatile("outb %0, %w1" : : "b" (val), "Nd" (port)); // expected-error {{invalid input size for constraint 'b'}}
  __asm__ volatile("outb %0, %w1" : : "c" (val), "Nd" (port)); // expected-error {{invalid input size for constraint 'c'}}
  __asm__ volatile("outb %0, %w1" : : "d" (val), "Nd" (port)); // expected-error {{invalid input size for constraint 'd'}}
  __asm__ volatile("outb %0, %w1" : : "S" (val), "Nd" (port)); // expected-error {{invalid input size for constraint 'S'}}
  __asm__ volatile("outb %0, %w1" : : "D" (val), "Nd" (port)); // expected-error {{invalid input size for constraint 'D'}}
  __asm__ volatile("foo1 %0" : : "A" (val128)); // expected-error {{invalid input size for constraint 'A'}}
  __asm__ volatile("foo1 %0" : : "f" (val256)); // expected-error {{invalid input size for constraint 'f'}}
  __asm__ volatile("foo1 %0" : : "t" (val256)); // expected-error {{invalid input size for constraint 't'}}
  __asm__ volatile("foo1 %0" : : "u" (val256)); // expected-error {{invalid input size for constraint 'u'}}
}
